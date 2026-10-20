"""Behavior tests for the shipped Lua processor; no live user database used.

2026-10 变更：数字已加入 speller/alphabet，以支持「拼音 + 数字/型号」混合造句。
随之而来的语义变化：

  · 小键盘数字（KP_0…KP_9）进入**真实输入串**，参与拼写与造句
      woshi + KP_4        -> input "woshi4"（Rime 可造出「我是4」）
      esp + KP_3 + KP_2   -> input "esp32"
  · 主键盘数字只用于选词，绝不进入输入串
      无虚拟尾巴时 -> 调 context:select(index)（代为执行 Rime 原生选择）
      有虚拟尾巴时 -> 走原来的虚拟 buffer 路径
  · 小键盘运算符与普通标点仍进入虚拟尾巴（不参与拼写）
"""
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

-- menu=false 用来模拟「组合中但没有候选」
local function fixture(input, menu)
  local c = {input=input or '', props={}, caret_pos=#(input or ''), menu=menu ~= false}
  c.update_notifier = {connect=function(_, callback)
    c.notify=callback; return {disconnect=function() end}
  end}
  function c:set_property(k,v) self.props[k]=v end
  function c:is_composing() return self.input ~= '' end
  function c:has_menu() return self.menu and self.input ~= '' end
  function c:clear() self.input=''; self.caret_pos=0; self.notify(self) end
  function c:push_input(s) self.input=self.input..s; self.caret_pos=#self.input; self.notify(self) end
  function c:select(i) self.sel = i end
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
    local r = processor.func({repr=function() return s end,release=function() return false end},env)
    -- 模拟 Rime：处理器放行的单个字母/数字会进真实输入
    if r == 2 then
      if s:match("^[A-Za-z]$") or s:match("^%d$") then c:push_input(s)
      elseif s == 'Home' then c.caret_pos = 0
      elseif s == 'End' then c.caret_pos = #c.input
      elseif s == 'Left' then c.caret_pos = math.max(0, c.caret_pos - 1)
      elseif s == 'Right' then c.caret_pos = math.min(#c.input, c.caret_pos + 1)
      end
    end
    return r
  end
  return c,env,key,outputs,seg
end

--------------------------------------------------------------------------
-- 1. 小键盘运算符：进虚拟尾巴，仍保持「混合分段」原有行为
--------------------------------------------------------------------------
for key,symbol in pairs({KP_Divide='/', KP_Multiply='*', KP_Subtract='-',
                         KP_Add='+', KP_Decimal='.'}) do
  -- 无组合时直接上屏
  local c,e,k,o=fixture(''); eq(k(key),1); eq(o[1],symbol); eq(e.tail,'')
  -- 组合中进尾巴，后续字母也留在尾巴里
  c,e,k,o=fixture('ni'); k(key); k('h'); k('a'); k('o')
  eq(c.input,'ni'); eq(e.tail,symbol..'hao'); eq(#o,0)
  -- 主键盘数字在有尾巴时并入虚拟 buffer
  k('1'); eq(e.buffer,'C0'..symbol); eq(c.input,'hao'); eq(#o,0)
  k('2'); eq(o[1],'C0'..symbol..'C1'); eq(e.tail,'')
end

--------------------------------------------------------------------------
-- 2. 小键盘数字：进入真实输入串（这是与上游的关键差异）
--------------------------------------------------------------------------
local c,e,k,o=fixture('woshi')
k('KP_4'); eq(c.input,'woshi4'); eq(e.tail,''); eq(#o,0)
-- 数字之后的字母继续累积在真实输入里，可被 Rime 当作新的音节
k('y'); k('a'); k('n'); k('g'); eq(c.input,'woshi4yang')
-- 型号词：esp + 3 + 2
c,e,k,o=fixture('esp'); k('KP_3'); k('KP_2'); eq(c.input,'esp32')
k('b'); k('i'); eq(c.input,'esp32bi')
-- 小数点仍走尾巴，因此后面的数字留在尾巴里
c,e,k,o=fixture('ni'); k('KP_Decimal'); eq(e.tail,'.'); k('KP_5'); eq(e.tail,'.5')
-- 空输入时小键盘数字直接上屏
c,e,k,o=fixture(''); eq(k('KP_4'),1); eq(o[1],'4'); eq(e.tail,'')
-- 光标在 0 位置时，数字并入前缀 buffer（原有行为保留）
c,e,k,o=fixture('ni'); c.caret_pos=0; k('KP_4')
eq(#o,0); eq(e.buffer,'4'); eq(c.input,'ni')

--------------------------------------------------------------------------
-- 3. 主键盘数字：只选词，绝不进输入串
--------------------------------------------------------------------------
-- 普通拼音组合（无虚拟尾巴）-> 代为执行原生选择，索引 0-based
c,e,k,o=fixture('nihao'); k('2'); eq(c.sel,1); eq(c.input,'nihao')
c,e,k,o=fixture('nihao'); k('1'); eq(c.sel,0)
-- 只接受「当前页可见」的数字：页大小 5 时，6-9/0 不选词、不进输入
c,e,k,o=fixture('nihao'); k('6'); eq(c.sel,nil); eq(c.input,'nihao')
c,e,k,o=fixture('nihao'); k('9'); eq(c.sel,nil); eq(c.input,'nihao')
c,e,k,o=fixture('nihao'); k('0'); eq(c.sel,nil); eq(c.input,'nihao')
-- 翻页后：1-5 是「当前页内」的相对位置，必须加上页偏移（回归：曾只选第一页）
c,e,k,o,s=fixture('nihao'); s.selected_index=5;  k('1'); eq(c.sel,5)
c,e,k,o,s=fixture('nihao'); s.selected_index=5;  k('2'); eq(c.sel,6)
c,e,k,o,s=fixture('nihao'); s.selected_index=10; k('1'); eq(c.sel,10)
c,e,k,o,s=fixture('nihao'); s.selected_index=0;  k('1'); eq(c.sel,0)
c,e,k,o,s=fixture('nihao'); s.selected_index=5;  k('6'); eq(c.sel,nil)
-- 组合中但没有候选 -> 吞掉，数字不进入输入
c,e,k,o=fixture('nihao', false); k('3'); eq(c.input,'nihao'); eq(c.sel,nil)
-- 未组合 -> 交还宿主（正常打出数字）
c,e,k,o=fixture(''); eq(k('3'),2)
-- 有虚拟尾巴 -> 走 buffer 路径，不调原生选择
c,e,k,o=fixture('ni'); k('KP_Add'); k('h'); k('a'); k('o'); k('1')
eq(e.buffer,'C0+'); eq(c.sel,nil); eq(c.input,'hao')

--------------------------------------------------------------------------
-- 4. 提交与选择
--------------------------------------------------------------------------
c,e,k,o=fixture('ni'); k('KP_Add'); k('h'); k('a'); k('o'); k('1'); k('2')
eq(o[1],'C0+C1'); eq(e.tail,'')
c,e,k,o=fixture('ni'); k('KP_Add'); k('h'); k('a'); k('o'); k('1'); k('space')
eq(o[1],'C0+C0')
-- 纯数字混合串：Enter 交回 Rime（处理器不吞）
c,e,k,o=fixture('ni'); k('KP_4'); eq(k('Return'),2)

--------------------------------------------------------------------------
-- 5. 修饰键与特殊键仍走原路
--------------------------------------------------------------------------
c,e,k,o=fixture('ni')
eq(k('Control+slash'),2); eq(k('Alt+plus'),2); eq(k('apostrophe'),2)
eq(k('Control+KP_Add'),2); eq(k('Shift+KP_Divide'),2)
-- 方向键在真实输入内移动
c,e,k,o=fixture('ni4'); k('Home'); eq(c.caret_pos,0); k('Right'); eq(c.caret_pos,1)

print('Keypad behavior: '..checks..' checks passed')
''')
