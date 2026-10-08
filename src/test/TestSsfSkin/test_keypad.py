"""Behavior tests for the shipped Lua processor; no live user database used."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path.home() / 'Documents/Codex/2026-09-11/new-chat-2/work/lua-test-runtime'))
from lupa import LuaRuntime

lua = LuaRuntime(unpack_returned_tuples=True)
lua.globals().processor_source = Path(sys.argv[1]).read_text(encoding='utf-8-sig')
lua.execute(r'''
local processor = assert(load(processor_source))()
local checks = 0
local function eq(a,b)
  checks = checks + 1
  assert(a == b, tostring(a).." ~= "..tostring(b))
end
local function fixture(input)
  local c = {input=input or '', props={}, caret_pos=#(input or '')}
  c.update_notifier = {connect=function(_, callback)
    c.notify=callback; return {disconnect=function() end}
  end}
  function c:set_property(k,v) self.props[k]=v end
  function c:is_composing() return self.input ~= '' end
  function c:has_menu() return self.input ~= '' end
  function c:clear() self.input=''; self.caret_pos=0; self.notify(self) end
  function c:push_input(s) self.input=self.input..s; self.caret_pos=#self.input; self.notify(self) end
  function c:select() error('select must never commit during virtual selection') end
  local seg={selected_index=0}
  function seg:get_candidate_at(i)
    return {text='C'..i, _end=c.consume or #c.input}
  end
  function c:get_selected_candidate() return seg:get_candidate_at(seg.selected_index) end
  c.composition={back=function() return seg end}
  local outputs={}
  local e={context=c, schema={config={get_int=function() return 5 end}}}
  function e:commit_text(s) outputs[#outputs+1]=s end
  local env={engine=e}; processor.init(env)
  local function key(s)
    return processor.func({repr=function() return s end,release=function() return false end},env)
  end
  return c,env,key,outputs,seg
end
-- Plain keypad input is a literal; arrows remain navigation.
for _, pair in ipairs({{'KP_Add','+'},{'KP_Subtract','-'},{'KP_Divide','/'},
                      {'KP_Multiply','*'},{'comma',','},{'period','.'}}) do
  local c,e,k,o=fixture('ni')
  k(pair[1]); k('h'); k('a'); k('o'); k('KP_7')
  eq(k('Return'),1); eq(o[1],'ni'..pair[2]..'hao7'); eq(#o,1)
end
for _, enter in ipairs({'Return', 'KP_Enter'}) do
  local c,e,k,o=fixture('ni')
  k('KP_4'); k('slash'); k('h'); k('a'); k('o')
  eq(k(enter),1); eq(o[1],'ni4/hao'); eq(c.input,'')
  eq(e.buffer,''); eq(e.tail,''); eq(e.raw_buffer,'')
  c,e,k,o=fixture('woshi')
  k('KP_5'); k('h'); k('a'); k('o'); k('1')
  eq(e.buffer,'C05'); eq(#o,0)
  eq(k(enter),1); eq(o[1],'woshi5hao'); eq(e.raw_buffer,'')
end
local c,e,k,o=fixture('')
eq(k('KP_4'),1); eq(o[1],'4'); eq(k('Left'),2)
-- Entire mixed sequence remains uncommitted until the final selection.
c,e,k,o=fixture('woshi')
k('KP_4'); k('y'); k('a'); k('n'); k('g'); k('KP_5'); k('h'); k('a'); k('n')
eq(c.input,'woshi'); eq(e.tail,'4yang5han'); eq(#o,0)
k('1'); eq(e.buffer,'C04'); eq(c.input,'yang'); eq(e.tail,'5han'); eq(#o,0)
k('2'); eq(e.buffer,'C04C15'); eq(c.input,'han'); eq(#o,0)
k('space'); eq(o[1],'C04C15C0'); eq(e.buffer,''); eq(e.tail,'')
-- Partial-word selection must not discard the unconverted pinyin.
c,e,k,o=fixture('woshi'); k('KP_4'); k('y'); c.consume=2
k('1'); eq(e.buffer,'C0'); eq(c.input,'shi'); eq(e.tail,'4y'); eq(#o,0)
-- Page-relative labels select the correct absolute candidate.
local s
c,e,k,o,s=fixture('ni'); k('KP_4'); s.selected_index=6; k('2')
eq(o[1],'C64'); eq(c.input,'')
-- Invalid page label neither commits nor passes through to the speller.
c,e,k,o=fixture('ni'); k('KP_4'); eq(k('9'),1); eq(#o,0); eq(e.tail,'4')
-- Focus cancellation clears both virtual parts.
c,e,k,o=fixture('ni'); k('KP_4'); c:clear(); eq(e.tail,''); eq(e.buffer,'')
-- Inserting inside the raw pinyin keeps the remainder after the number.
c,e,k,o=fixture('nihao'); c.caret_pos=2; k('KP_7')
eq(c.input,'ni'); eq(e.tail,'7hao'); eq(e.tail_cursor,1)
k('a'); eq(e.tail,'7ahao'); k('BackSpace'); eq(e.tail,'7hao')
-- Each keypad operator behaves like a digit, including insertion and deletion.
for key,symbol in pairs({KP_Divide='/', KP_Multiply='*', KP_Subtract='-',
                         KP_Add='+', KP_Decimal='.'}) do
  c,e,k,o=fixture(''); eq(k(key),1); eq(o[1],symbol); eq(e.tail,'')
  c,e,k,o=fixture('ni'); k(key); k('h'); k('a'); k('o')
  eq(c.input,'ni'); eq(e.tail,symbol..'hao'); eq(#o,0)
  k('1'); eq(e.buffer,'C0'..symbol); eq(c.input,'hao'); eq(#o,0)
  k('2'); eq(o[1],'C0'..symbol..'C1'); eq(e.tail,'')
  c,e,k,o=fixture('nihao'); c.caret_pos=2; k(key)
  eq(e.tail,symbol..'hao'); eq(e.tail_cursor,1)
  k('BackSpace'); eq(c.input,'nihao'); eq(e.tail,''); eq(#o,0)
end
-- Adjacent operators/digits are kept in order and committed only at the end.
c,e,k,o=fixture('ni')
k('KP_Divide'); k('KP_2'); k('KP_Multiply'); k('KP_Subtract'); k('KP_Add');
k('KP_Decimal'); k('h'); k('a'); k('o')
eq(e.tail,'/2*-+.hao'); k('1'); eq(e.buffer,'C0/2*-+.'); eq(#o,0)
k('space'); eq(o[1],'C0/2*-+.C0')
-- Ordinary punctuation is deferred between pinyin segments just like keypad input.
for key,symbol in pairs({slash='/',plus='+',comma=',',period='.',semicolon=';',
  colon=':',quotedbl='"',exclam='!',question='?',bracketleft='[',bracketright=']',
  braceleft='{',braceright='}',backslash='\\',bar='|',less='<',greater='>',
  at='@',numbersign='#',dollar='$',percent='%',asciicircum='^',ampersand='&',
  asterisk='*',parenleft='(',parenright=')',underscore='_',grave='`',asciitilde='~'}) do
  c,e,k,o=fixture(''); eq(k(key),1); eq(o[1],symbol)
  c,e,k,o=fixture('nihao'); c.caret_pos=2; eq(k(key),1)
  eq(c.input,'ni'); eq(e.tail,symbol..'hao'); eq(e.tail_cursor,1); eq(#o,0)
  k('1'); eq(e.buffer,'C0'..symbol); eq(c.input,'hao'); eq(#o,0)
  k('2'); eq(o[1],'C0'..symbol..'C1'); eq(e.tail,'')
  c,e,k,o=fixture('nihao'); c.caret_pos=2; k(key); k('BackSpace')
  eq(c.input,'nihao'); eq(e.tail,'')
end
c,e,k,o=fixture('ni'); k('Shift+exclam'); k('KP_4'); k('slash'); k('h')
eq(e.tail,'!4/h'); k('1'); eq(e.buffer,'C0!4/'); eq(#o,0)
k('space'); eq(o[1],'C0!4/C0')
-- Modifier shortcuts and the pinyin apostrophe keep their normal route.
c,e,k,o=fixture('ni'); eq(k('Control+slash'),2); eq(k('Alt+plus'),2)
eq(k('apostrophe'),2)
eq(k('Control+KP_Add'),2); eq(k('Shift+KP_Divide'),2)
-- Arrow movement crosses the boundary in both directions without editing.
c,e,k,o=fixture('ni'); k('slash'); k('h'); k('a'); k('o')
for i=1,5 do k('Left') end
eq(c.caret_pos,1); eq(e.tail_cursor,0); eq(e.tail,'/hao')
eq(c.props.weasel_keypad_cursor,'-1'); eq(#o,0)
k('x'); eq(c.input,'nxi'); eq(c.caret_pos,2); eq(e.tail,'/hao')
k('BackSpace'); eq(c.input,'ni'); eq(c.caret_pos,1)
k('Right'); eq(c.caret_pos,2); eq(c.props.weasel_keypad_cursor,'0')
k('Right'); eq(e.tail_cursor,1); eq(c.props.weasel_keypad_cursor,'1')
k('Right'); k('x'); eq(e.tail,'/hxao'); eq(e.tail_cursor,3)
k('BackSpace'); eq(e.tail,'/hao')
-- Deleting the separator joins the pinyin at the correct caret, not the end.
k('Home'); eq(c.caret_pos,0); eq(e.tail_cursor,0)
k('Right'); k('Right'); k('Delete')
eq(c.input,'nihao'); eq(c.caret_pos,2); eq(e.tail,''); eq(#o,0)
-- Inserting another literal inside the first segment preserves every suffix.
c,e,k,o=fixture('ni'); k('KP_4'); k('h'); k('a'); k('o')
k('Home'); k('Right'); k('KP_7')
eq(c.input,'n'); eq(e.tail,'7i4hao'); eq(e.tail_cursor,1)
k('1'); eq(e.buffer,'C07'); eq(c.input,'i'); eq(e.tail,'4hao')
k('1'); eq(e.buffer,'C07C04'); eq(c.input,'hao'); eq(#o,0)
k('1'); eq(o[1],'C07C04C0')
-- Deleting the first whole segment retains exposed literals with the buffer.
c,e,k,o=fixture('n'); k('KP_4'); k('h'); k('a'); k('o')
k('Home'); k('Delete'); eq(e.buffer,'4'); eq(c.input,'hao'); eq(#o,0)
k('1'); eq(o[1],'4C0')
-- Navigation clamps at both ends; unrelated shortcuts pass through.
c,e,k,o=fixture('ni'); k('KP_4'); k('h')
k('Home'); k('Left'); eq(c.caret_pos,0)
k('End'); k('Right'); eq(e.tail_cursor,2)
eq(k('Control+Left'),2); eq(k('Up'),2); eq(#o,0)
-- Regression cases found by the 2026-09-26 review.
c,e,k,o=fixture('n'); k('KP_4'); k('Home'); k('Delete')
eq(#o,0); eq(c.input,'4'); eq(c.caret_pos,0); eq(e.literal_only,true)
k('Delete'); eq(c.input,''); eq(#o,0); eq(e.literal_only,false)
c,e,k,o=fixture('ni'); k('KP_4'); k('CapsLock+A')
eq(e.tail,'4A'); k('CapsLock+BackSpace'); eq(e.tail,'4')
c,e,k,o=fixture('ni'); k('KP_4'); k('h'); k('a'); k('o'); k('1')
k('Home'); eq(c.input,'ni'); eq(e.tail,'4hao'); eq(c.caret_pos,0)
k('Return'); eq(o[1],'ni4hao')
c,e,k,o=fixture('ni'); c.caret_pos=0; k('KP_4')
eq(#o,0); eq(e.buffer,'4'); eq(c.input,'ni')
k('Home'); eq(c.props.weasel_keypad_prefix_cursor,'0')
k('Delete'); eq(c.input,'ni'); eq(e.buffer,''); eq(#o,0)
c,e,k,o=fixture('ni'); k('KP_4'); eq(k('minus'),2); eq(k('equal'),2)
eq(e.tail,'4')
print('Keypad behavior: '..checks..' checks passed')
''')
