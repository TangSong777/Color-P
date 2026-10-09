-- Keep physical keypad digits and operators visible in the preedit without feeding them to
-- the speller.  `tail` starts at the first keypad literal: the live Rime input
-- before it remains the current candidate segment, and every later letter is
-- held for the next segment.  Selected segments are accumulated in `buffer`
-- rather than committed immediately, so `woshi4yang` stays visibly composed
-- as `我是4yang` while `yang` is still being selected.
local keypad_input = {}

local keypad_literals = {
  KP_0 = "0", KP_1 = "1", KP_2 = "2", KP_3 = "3", KP_4 = "4",
  KP_5 = "5", KP_6 = "6", KP_7 = "7", KP_8 = "8", KP_9 = "9",
  KP_Divide = "/", KP_Multiply = "*", KP_Subtract = "-", KP_Add = "+",
  KP_Decimal = ".",
}

-- Ordinary punctuation joins the same deferred composition as keypad input.
-- Apostrophe remains the pinyin syllable separator. The key binder, which
-- follows this processor and owns unshifted minus/equal while a menu exists.
local punctuation_literals = {
  exclam = "!", quotedbl = '"', numbersign = "#", dollar = "$",
  percent = "%", ampersand = "&", parenleft = "(", parenright = ")",
  asterisk = "*", plus = "+", comma = ",", minus = "-", period = ".",
  slash = "/", colon = ":", semicolon = ";", less = "<", equal = "=",
  greater = ">", question = "?", at = "@", bracketleft = "[",
  backslash = "\\", bracketright = "]", asciicircum = "^", underscore = "_",
  grave = "`", braceleft = "{", bar = "|", braceright = "}", asciitilde = "~",
}
local function punctuation_value(repr)
  -- Shift may remain in Rime's representation of an already translated
  -- printable keysym. Never capture Ctrl/Alt/Super shortcuts.
  return punctuation_literals[repr:gsub("^Shift%+", "")]
end

-- A digit separator typed directly after a digit must stay a separator.
-- Committing the configured Chinese punctuation here turned "123.4" into
-- "123。4" and "192.168.1.1" into "192。168。1。1", because this processor runs
-- before Rime's punctuator and therefore pre-empted its
-- "digit_separators: ,.:" rule.
--
-- env.last_commit records the literal this processor committed most recently:
-- a Lua processor cannot read what is already in the document.
local function is_digit_separator(value)
  return value == "." or value == "," or value == ":"
end
local function follows_digit(env, value)
  return is_digit_separator(value) and env.last_commit ~= nil and
      env.last_commit:match("^[0-9]$") ~= nil
end
-- True when the key must be left to the punctuator instead of being committed
-- here. With ascii_punct on the raw ASCII form is wanted anyway, so the
-- processor keeps owning the key; otherwise the punctuator's digit_separators
-- rule yields "123.4" in half shape and the shape formatter yields "１２３．４"
-- in full shape.
local function separator_owned_by_punctuator(env, value)
  return follows_digit(env, value) and
      not (env.engine.context.get_option and
           env.engine.context:get_option("ascii_punct"))
end
local function remember_commit(env, text)
  env.last_commit = text
end

-- Rime uses byte offsets; editing must step over whole UTF-8 codepoints.
local function next_pos(text, pos)
  if pos >= #text then return #text end
  return (utf8.offset(text, 2, pos + 1) or (#text + 1)) - 1
end
local function prev_pos(text, pos)
  if pos <= 0 then return 0 end
  return (utf8.offset(text, -1, pos + 1) or 1) - 1
end
local function leading_literals(text)
  local last = 0
  while last < #text do
    local following = next_pos(text, last)
    local char = text:sub(last + 1, following)
    if char:match("^[A-Za-z']$") then break end
    last = following
  end
  return last > 0 and text:sub(1, last) or nil
end
local function insert_at(text, position, value)
  return text:sub(1, position) .. value .. text:sub(position + 1)
end
local function erase_at(text, position)
  return text:sub(1, position) .. text:sub(next_pos(text, position) + 1)
end

local function is_letter_or_apostrophe(key_repr)
  return key_repr:match("^[A-Za-z]$")
      or key_repr:match("^Shift%+[A-Za-z]$")
      or key_repr == "apostrophe"
end

local function letter_value(key_repr)
  if key_repr == "apostrophe" then
    return "'"
  end
  return key_repr:match("([A-Za-z])$") or key_repr
end

function keypad_input.init(env)
  env.tail = ""
  env.tail_cursor = 0
  env.buffer = ""
  env.raw_buffer = ""
  env.selections = {}
  env.literal_only = false
  env.shift_armed = false
  env.transitioning = false
  -- Most recent literal this processor committed; see follows_digit().
  env.last_commit = nil
  local context = env.engine.context

  env.quote_side = {}
  env.punctuation = function(repr)
    local value = punctuation_value(repr) or (repr == "apostrophe" and "'")
    if not value then return nil end
    if context.get_option and context:get_option("ascii_punct") then return value end
    local config = env.engine.schema.config
    if not config.get_string then return value end
    local shape = context.get_option and context:get_option("full_shape") and "full_shape" or "half_shape"
    local path = "punctuator/" .. shape .. "/" .. value
    if value == "/" then return value end
    local mapped = config:get_string(path)
    if mapped and mapped ~= "" then return mapped end
    local opening = config:get_string(path .. "/pair/@0")
    local closing = config:get_string(path .. "/pair/@1")
    if opening and closing then
      local close = env.quote_side[value]
      env.quote_side[value] = not close
      return close and closing or opening
    end
    return value
  end

  local function publish_tail()
    -- These properties are read by the Weasel UI only.  Rime's context.input
    -- contains just the active pinyin segment, so candidate lookup is never
    -- polluted by keypad numbers or following segments.
    context:set_property("weasel_keypad_suffix", env.tail)
    local active_cursor = env.tail_cursor == 0 and
        (context.caret_pos or #(context.input or "")) < #(context.input or "")
    context:set_property("weasel_keypad_cursor", active_cursor and "-1" or tostring(env.tail_cursor))
    context:set_property("weasel_keypad_prefix", env.buffer)
    context:set_property("weasel_keypad_literal_only", env.literal_only and "1" or "")
    context:set_property("weasel_keypad_prefix_cursor", env.prefix_cursor and tostring(env.prefix_cursor) or "")
  end
  env.publish_tail = publish_tail

  local function clear_tail()
    env.tail = ""
    env.tail_cursor = 0
    publish_tail()
  end
  env.clear_tail = clear_tail

  local function clear_virtual_state()
    env.tail = ""
    env.tail_cursor = 0
    env.buffer = ""
    env.raw_buffer = ""
    env.selections = {}
    env.literal_only = false
    env.prefix_cursor = nil
    publish_tail()
  end
  env.clear_virtual_state = clear_virtual_state

  local function replace_active_input(input)
    -- Context:clear() immediately invokes update_notifier.  While the first
    -- keypad literal is being moved into the virtual suffix, that callback must
    -- not mistake the temporary empty context for a user cancellation and
    -- erase the suffix.  Keep the whole replacement atomic from Lua's view.
    env.transitioning = true
    context:clear()
    if input ~= "" then
      context:push_input(input)
    end
    env.transitioning = false
  end
  env.replace_active_input = replace_active_input

  env.learn_selections = function()
    if not context.get_option or not context.set_option then return end
    local old_dumb = context:get_option("dumb")
    local old_auto = context:get_option("_auto_commit")
    env.transitioning = true
    context:set_option("dumb", true)
    context:set_option("_auto_commit", false)
    for _, choice in ipairs(env.selections) do
      context:clear()
      context:push_input(choice.raw)
      if context:has_menu() then
        local segment = context.composition:back()
        for i = 0, 127 do
          local candidate = segment:get_candidate_at(i)
          if not candidate then break end
          if candidate.text == choice.text and candidate._end == #choice.raw then
            context:select(i)
            context:commit() -- emits native dictionary-learning notification
            break
          end
        end
      end
    end
    context:clear()
    context:set_option("_auto_commit", old_auto)
    context:set_option("dumb", old_dumb)
    env.transitioning = false
  end

  -- Reopening a confirmed prefix restores its original spelling. This makes
  -- Home/Left/Backspace cross a selected-word boundary without losing text.
  env.reopen_prefix = function(position)
    local raw = env.raw_buffer .. (context.input or "") .. env.tail
    env.buffer, env.raw_buffer, env.selections = "", "", {}
    env.edit_raw(raw, position)
  end

  local function accept_current_candidate(candidate)
    -- A virtual buffer with no remaining tail is the final deferred segment.
    -- Otherwise the tail must begin with literal keypad digits and operators, followed by
    -- the next pinyin segment and (optionally) later keypad content.
    if env.tail == "" and env.buffer == "" then
      return false
    end
    if not context:has_menu() then
      return false
    end

    local literal, next_segment, remaining = "", "", ""
    if env.tail ~= "" then
      literal = leading_literals(env.tail)
      if not literal then
        return false
      end
      local rest = env.tail:sub(#literal + 1)
      next_segment = rest:match("^([A-Za-z']*)") or ""
      remaining = rest:sub(#next_segment + 1)
    end

    local selected = candidate or context:get_selected_candidate()
    if not selected or not selected.text or selected.text == "" then
      return false
    end

    -- A candidate can cover only part of the active pinyin. Preserve its
    -- unconverted remainder before advancing past the next keypad boundary.
    local input = context.input or ""
    local consumed = math.max(0, math.min(selected._end or #input, #input))
    if consumed == 0 then return false end
    env.selections[#env.selections + 1] = {
      raw = input:sub(1, consumed), text = selected.text
    }
    local pending_pinyin = input:sub(consumed + 1)
    if pending_pinyin ~= "" then
      env.buffer = env.buffer .. selected.text
      env.raw_buffer = env.raw_buffer .. input:sub(1, consumed)
      env.replace_active_input(pending_pinyin)
      publish_tail()
      return true
    end

    env.buffer = env.buffer .. selected.text .. literal
    env.raw_buffer = env.raw_buffer .. input:sub(1, consumed) .. literal
    env.tail = remaining
    -- The next real pinyin segment starts just before any future keypad tail.
    -- New letters therefore extend that segment, while new keypad digits and operators are
    -- inserted at the same visible cursor position.
    env.tail_cursor = 0
    env.transitioning = true
    context:clear()
    if next_segment ~= "" then
      context:push_input(next_segment)
    end
    env.transitioning = false

    if next_segment == "" and env.tail == "" then
      local output = env.buffer
      env.learn_selections()
      env.selections = {}
      env.buffer = ""
      env.raw_buffer = ""
      env.tail_cursor = 0
      publish_tail()
      -- Only the final selected segment reaches the application.  Earlier
      -- selections remain in the visible input bar and can never leak as a
      -- partial commit.
      env.engine:commit_text(output)
    else
      publish_tail()
    end
    return true
  end
  env.accept_current_candidate = accept_current_candidate

  local function normalize_tail()
    -- Removing the first keypad literal joins following letters back to the
    -- current pinyin segment.  This preserves normal editing semantics.
    local leading_letters = env.tail:match("^([A-Za-z']+)")
    if leading_letters and leading_letters ~= "" and context:is_composing() then
      context:push_input(leading_letters)
      env.tail = env.tail:sub(#leading_letters + 1)
      env.tail_cursor = math.max(0, env.tail_cursor - #leading_letters)
    end
  end
  env.normalize_tail = normalize_tail

  -- One editable coordinate space for all unconverted letters and literals.
  -- Keep the real Rime caret in the active segment, or at its end while the
  -- cursor is in the virtual suffix. Repartition only after actual edits.
  env.move_cursor = function(position)
    local length = #(context.input or "")
    env.prefix_cursor = position < 0 and env.buffer == env.raw_buffer and
        math.max(0, #env.buffer + position) or nil
    position = math.max(0, math.min(position, length + #env.tail))
    context.caret_pos = math.min(position, length)
    env.tail_cursor = math.max(0, position - length)
    publish_tail()
  end
  env.edit_raw = function(raw, position)
    local original_position = position
    local previous_buffer, previous_raw = env.buffer, env.raw_buffer
    env.prefix_cursor = nil
    local leading = leading_literals(raw) or ""
    -- If deleting the entire first pinyin segment exposes leading literals,
    -- retain them with the confirmed prefix while the next segment is active.
    env.buffer = env.buffer .. leading
    env.raw_buffer = env.raw_buffer .. leading
    raw = raw:sub(#leading + 1)
    position = position - #leading
    local active = raw:match("^([A-Za-z']*)") or ""
    env.tail = raw:sub(#active + 1)
    env.replace_active_input(active)
    if active == "" then
      -- An edit is never a commit. Keep exposed literal text in a real Rime
      -- composition so Escape/focus-loss and subsequent editing still work.
      local raw_literals = previous_raw .. leading .. env.tail
      -- Reopen a preceding confirmed word if deleting the final active word
      -- would otherwise leave it stranded outside Rime's composition.
      env.buffer, env.raw_buffer, env.tail = "", "", ""
      env.selections = {}
      if raw_literals:find("[A-Za-z']") then
        env.edit_raw(raw_literals, #previous_raw + original_position)
        return
      end
      env.literal_only = raw_literals ~= ""
      env.replace_active_input(raw_literals)
      context.caret_pos = math.max(0, math.min(#raw_literals, #previous_raw + original_position))
      env.publish_tail()
    else
      env.literal_only = false
      env.move_cursor(position)
    end
  end

  -- Escape, clicking elsewhere, schema changes, and focus loss cancel the
  -- composition.  Never let a held virtual buffer leak into an unrelated
  -- input.
  env.update_connection = context.update_notifier:connect(function(ctx)
    if not env.transitioning and
        (env.tail ~= "" or env.buffer ~= "" or env.literal_only) and not ctx:is_composing() then
      clear_virtual_state()
    end
  end)
end

function keypad_input.func(key, env)
  local context = env.engine.context
  -- Lock is a state, not a shortcut modifier. Keep Ctrl/Alt/Super untouched.
  local key_repr = key:repr():gsub("CapsLock%+", ""):gsub("Lock%+", "")
  local bare = key_repr:gsub("Release%+", ""):gsub("^Shift%+", "")
  local mixed = env.tail ~= "" or env.buffer ~= "" or env.literal_only
  local shift = bare == "Shift_L" or bare == "Shift_R"
  if shift and mixed then
    if key:release() then
      if env.shift_armed and context.get_option then
        local output = env.raw_buffer .. (context.input or "") .. env.tail
        env.clear_virtual_state()
        context:clear()
        if output ~= "" then env.engine:commit_text(output) end
        context:set_option("ascii_mode", not context:get_option("ascii_mode"))
      end
      env.shift_armed = false
    else
      env.shift_armed = true
    end
    return 1
  end
  if not key:release() then env.shift_armed = false end
  if key:release() then return 2 end
  if context.get_option and context:get_option("ascii_mode") then return 2 end
  -- 没有候选框（没输入过字母）时，小键盘字符和标点都直接上屏。
  -- 有候选框时走下面的混合输入分支，塞进候选框。
  if not context:is_composing() then
    -- 数字后面的 , . : 表示小数点或千分位，必须保持 ASCII，否则 123.4 会变成
    -- 123。4。方案里的 punctuator/digit_separators 本就规定了这个例外。
    local plain = punctuation_value(key_repr)
    if separator_owned_by_punctuator(env, plain) then
      env.clear_virtual_state()
      env.engine:commit_text(plain)
      remember_commit(env, plain)
      return 1
    end
    -- 其余标点直接提交配置里的形态（半角/全角由方案与当前中英状态决定）。
    -- 从前这里把标点交给 punctuator（return 2），但实测 punctuator 并不提交它：
    -- 键被吃掉、字符留在 input 里，既没上屏也没候选框，逗号就这样“消失”了。
    local literal = keypad_literals[key_repr] or env.punctuation(key_repr) or key_repr:match("^[0-9]$")
    if literal then
      env.clear_virtual_state()
      env.engine:commit_text(literal)
      remember_commit(env, literal)
      return 1
    end
  end
  -- The key binder now follows this processor. Preserve its paging/shortcut
  -- ownership instead of turning top-row -/= into mixed literals.
  if context:has_menu() and (key_repr == "minus" or key_repr == "equal") then
    return 2
  end
  if mixed and key_repr == "Escape" then
    env.clear_virtual_state(); context:clear(); return 1
  end
  if env.prefix_cursor ~= nil then
    local raw = env.raw_buffer .. (context.input or "") .. env.tail
    local pos = env.prefix_cursor
    local value = keypad_literals[key_repr] or (key_repr ~= "apostrophe" and env.punctuation(key_repr)) or
        (is_letter_or_apostrophe(key_repr) and letter_value(key_repr))
    local handled = true
    if value then raw = insert_at(raw, pos, value); pos = pos + #value
    elseif key_repr == "BackSpace" then
      if pos > 0 then pos = prev_pos(raw, pos); raw = erase_at(raw, pos) end
    elseif key_repr == "Delete" then
      if pos < #raw then raw = erase_at(raw, pos) end
    elseif key_repr == "Left" then pos = prev_pos(raw, pos)
    elseif key_repr == "Right" then pos = next_pos(raw, pos)
    elseif key_repr == "Home" then pos = 0
    elseif key_repr == "End" then pos = #raw
    else handled = false end
    if handled then
      env.buffer, env.raw_buffer, env.selections = "", "", {}
      env.edit_raw(raw, pos)
      return 1
    end
  end
  if env.buffer ~= "" then
    local pos = env.tail_cursor > 0 and #(context.input or "") + env.tail_cursor
        or context.caret_pos or #(context.input or "")
    if key_repr == "Home" or ((key_repr == "Left" or key_repr == "BackSpace") and pos == 0) then
      local restored = key_repr == "Home" and 0 or #env.raw_buffer
      env.reopen_prefix(restored)
      if key_repr == "BackSpace" and restored > 0 then
        local raw = (context.input or "") .. env.tail
        local previous = prev_pos(raw, restored)
        env.edit_raw(erase_at(raw, previous), previous)
      end
      return 1
    end
  end
  local keypad_value = keypad_literals[key_repr]
  if (key_repr == "Return" or key_repr == "KP_Enter") and
      (env.tail ~= "" or env.buffer ~= "" or env.literal_only) then
    local output = env.raw_buffer .. (context.input or "") .. env.tail
    env.clear_virtual_state()
    context:clear()
    if output ~= "" then env.engine:commit_text(output) end
    return 1
  end
  if context:is_composing() then
    keypad_value = keypad_value or (key_repr ~= "apostrophe" and env.punctuation(key_repr))
  end

  -- Keep the top-row candidate keys as the only numbered candidate selector.
  -- When a keypad-tail composition is active, handle that selection before
  -- Rime's ordinary selector can commit it to the application. Read the
  -- candidate directly and append it to the virtual input-bar buffer.
  if not env.literal_only and context:has_menu() and (env.tail ~= "" or env.buffer ~= "") then
    local top_row_index = {
      ["1"] = 0, ["2"] = 1, ["3"] = 2, ["4"] = 3, ["5"] = 4,
      ["6"] = 5, ["7"] = 6, ["8"] = 7, ["9"] = 8, ["0"] = 9,
    }
    local index = top_row_index[key_repr]
    if index ~= nil then
      -- Context:select emits select_notifier and can immediately commit via
      -- Rime's editor. Read the candidate without firing that notifier.
      local segment = context.composition:back()
      local page_size = env.engine.schema.config:get_int("menu/page_size") or 5
      page_size = math.max(1, page_size)
      local page_start = math.floor(segment.selected_index / page_size) * page_size
      local candidate = index < page_size and segment:get_candidate_at(page_start + index)
      if candidate and env.accept_current_candidate(candidate) then
        return 1
      end
      return 1
    end
    if key_repr == "space" and
        env.accept_current_candidate() then
      return 1
    end
  end

  if env.literal_only and key_repr == "space" then
    local output = context.input or ""
    env.clear_virtual_state(); context:clear()
    if output ~= "" then env.engine:commit_text(output) end
    return 1
  end
  if (env.tail ~= "" or env.buffer ~= "" or env.literal_only) and context:is_composing() then
    local input = context.input or ""
    local raw = input .. env.tail
    local position = env.tail_cursor > 0 and (#input + env.tail_cursor) or
        math.max(0, math.min(context.caret_pos or #input, #input))
    local value = keypad_value or
        (is_letter_or_apostrophe(key_repr) and letter_value(key_repr))
    if value then
      env.edit_raw(insert_at(raw, position, value), position + #value)
      return 1
    end
    if key_repr == "BackSpace" then
      if position > 0 then
        local previous = prev_pos(raw, position)
        env.edit_raw(erase_at(raw, previous), previous)
      end
      return 1
    end
    if key_repr == "Delete" then
      if position < #raw then env.edit_raw(erase_at(raw, position), position) end
      return 1
    end
    if key_repr == "Left" then
      env.move_cursor(prev_pos(raw, position))
      return 1
    end
    if key_repr == "Right" then
      env.move_cursor(next_pos(raw, position))
      return 1
    end
    if key_repr == "Home" then
      env.move_cursor(0)
      return 1
    end
    if key_repr == "End" then
      env.move_cursor(#raw)
      return 1
    end
  end

  if not keypad_value then
    return 2
  end

  -- Rime's stock pipeline does not translate keypad key symbols (KP_0 …
  -- KP_9) to ordinary digits and operators when no pinyin composition exists. Returning 2
  -- here therefore makes them disappear in many TSF hosts. Commit the literal
  -- literal ourselves so the keypad is usable outside a candidate sequence.
  if not context:is_composing() then
    if separator_owned_by_punctuator(env, keypad_value) then
      -- Keypad "." right after a digit is a decimal point, not a full stop.
      return 2
    end
    env.clear_virtual_state()
    env.engine:commit_text(keypad_value)
    remember_commit(env, keypad_value)
    return 1
  end

  -- Split the current input at its actual Rime caret.  The keypad number is
  -- rendered exactly at that insertion point; the caret then advances into
  -- the virtual tail and later letters follow it there.
  local input = context.input or ""
  local caret = math.max(0, math.min(context.caret_pos or #input, #input))
  local before = input:sub(1, caret)
  local after = input:sub(caret + 1)
  if before == "" then
    env.edit_raw(keypad_value .. after, #keypad_value)
    return 1
  end

  env.tail = keypad_value .. after
  env.tail_cursor = #keypad_value
  env.replace_active_input(before)
  env.publish_tail()
  return 1
end

function keypad_input.fini(env)
  if env.update_connection then
    env.update_connection:disconnect()
  end
end

return keypad_input
