"""keypad_input.lua 每次按键开销基准。

只测 Lua 侧本身的开销（处理器逻辑、字符串处理、表查找），
不涉及 C++/IPC/UI —— 那些在本会话无法插桩。

用法: python bench_keypad.py <keypad_input.lua> [每个用例的迭代数]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path.home() / 'Documents/Codex/2026-09-11/new-chat-2/work/lua-test-runtime'))
from lupa import LuaRuntime

SRC = Path(sys.argv[1]).read_text(encoding='utf-8-sig')
ITERS = int(sys.argv[2]) if len(sys.argv) > 2 else 20000

lua = LuaRuntime(unpack_returned_tuples=True)
lua.globals().processor_source = SRC

# 桩件：尽量贴近真实调用面，但不做多余工作，避免桩件本身成为瓶颈
lua.execute(r'''
local processor = assert(load(processor_source))()

-- repr 表预先建好，避免桩件里做字符串拼接影响测量
local REPS = {}
for _, r in ipairs({
  "a","b","c","n","i","h","o","g","KP_4","KP_7","KP_Add","KP_Decimal",
  "comma","period","slash","semicolon","BackSpace","Escape","Return",
  "Shift_L","Shift_R","space","minus","equal","Left","Right","Home","End",
  "Control+BackSpace","Shift+BackSpace","CapsLock+A","apostrophe",
  "numbersign","colon","quotedbl","exclam","question","bracketleft",
}) do
  REPS[r] = { repr = function() return r end, release = function() return false end }
end

function make_fixture(input)
  local c = {input=input or '', props={}, caret_pos=#(input or '')}
  c.update_notifier = {connect=function(_, cb) c.notify=cb; return {disconnect=function() end} end}
  function c:set_property(k,v) self.props[k]=v end
  function c:get_option(k) return false end
  function c:set_option(k,v) end
  function c:is_composing() return self.input ~= '' end
  function c:has_menu() return self.input ~= '' end
  function c:clear() self.input=''; self.caret_pos=0; self.notify(self) end
  function c:push_input(s) self.input=self.input..s; self.caret_pos=#self.input; self.notify(self) end
  local seg={selected_index=0}
  function seg:get_candidate_at(i) return {text='C'..i, _end=c.consume or #c.input} end
  c.composition={back=function() return seg end}
  local e={context=c, schema={config={get_int=function() return 5 end,
                                      get_string=function() return nil end}}}
  function e:commit_text(s) end
  local env={engine=e}
  processor.init(env)
  return env
end

local function bench(rep, input, iters)
  local env = make_fixture(input)
  local key = REPS[rep]
  local func = processor.func
  local t0 = os.clock()
  for i = 1, iters do func(key, env) end
  return (os.clock() - t0) / iters
end

-- 返回 秒/次
function run_case(rep, input, iters)
  return bench(rep, input, iters)
end
''')

CASES = [
    ("letter a (无组合)", "a", ""),
    ("letter n (有组合)", "n", "ni"),
    ("KP_4 (有组合)", "KP_4", "ni"),
    ("comma (无组合)", "comma", ""),
    ("comma (有组合)", "comma", "ni"),
    ("BackSpace (有组合)", "BackSpace", "ni"),
    ("Escape (有组合)", "Escape", "ni"),
    ("Shift_L (无组合)", "Shift_L", ""),
    ("Shift+BackSpace (有组合)", "Shift+BackSpace", "ni"),
    ("Control+BackSpace (有组合)", "Control+BackSpace", "ni"),
    ("slash (模拟无候选框标点)", "slash", ""),
]

run_case = lua.globals().run_case
print(f"{'用例':<34} {'us/key':>9}  {'相对':>6}")
print("-" * 56)
results = {}
for label, rep, inp in CASES:
    # 预热
    run_case(rep, inp, 2000)
    best = min(run_case(rep, inp, ITERS) for _ in range(3))
    us = best * 1e6
    results[label] = us
    print(f"{label:<34} {us:>9.3f}")

base = results["letter a (无组合)"]
print("-" * 56)
print(f"基准(普通字母) = {base:.3f} us/key")
