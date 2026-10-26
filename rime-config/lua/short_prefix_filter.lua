-- 单字母前缀补全的抑制
--
-- 白名单里很多词以单个字母开头（Ubuntu / Undertale / Intel / Vim ...），
-- 补全和造句会把它们接进整句，于是误触一个字母就毁掉整句话：
--
--   uqnali  →  Ubuntu去哪里     （本意是 qunali → 去那里）
--   i...    →  Intel...
--
-- 判据：**第一个音节只有 1 个字符**（u、i、v 这类）且**候选开头的连续英文字母
-- 至少有 2 个**时丢弃。
--
-- 音节边界要看 preedit（带自动分隔符），而不是 context.input（原始输入不带）：
--   uqnali 的 preedit 是 u'q'na'li，第一个音节就是 u。
--
--   u        →  有 | 又 | 由            （Ubuntu 丢弃）
--   uqnali   →  去哪里                   （Ubuntu去哪里 丢弃）
--   i        →  一 | 以 | 已             （Intel / iOS版 丢弃）
--   x        →  X光 | X档案              （开头只有 1 个字母，保留）
--   intel    →  Intel                   （首音节 5 个字符，不触发）
--   chat     →  ChatGPT                 （首音节 4 个字符，不触发）
--   ubun     →  Ubuntu                  （首音节 4 个字符，不触发）

local function leading_ascii_len(text)
  local run = text:match("^([A-Za-z]+)")
  return run and #run or 0
end

-- 第一个音节是否只有一个字母：看 preedit 的首段（分隔符是 '）
local function has_single_letter_first_syllable(env)
  local ok, preedit = pcall(function() return env.engine.context:get_preedit() end)
  local text = ""
  if ok and preedit then
    text = preedit.text or ""
  end
  if text == "" then
    -- 退路：直接用原始输入判断（单字母输入时它本身就是首音节）
    local raw = env.engine.context.input or ""
    return #raw == 1 and raw:match("^[A-Za-z]$") ~= nil
  end
  local first = text:match("^([^']*)") or ""
  return #first == 1 and first:match("^[A-Za-z]$") ~= nil
end

local function filter(input, env)
  local short_prefix = has_single_letter_first_syllable(env)
  for cand in input:iter() do
    local text = cand.text or ""
    if short_prefix and leading_ascii_len(text) >= 2 then
      -- 单字母音节接出来的英文词/造句，丢弃
    else
      yield(cand)
    end
  end
end

return filter
