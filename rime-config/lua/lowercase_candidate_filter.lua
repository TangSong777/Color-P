-- 英文候选的大小写双份
--
-- 白名单里的词按规范大小写存储（GitHub、VSCode、ChatGPT），但有时需要全小写
-- 形式（写代码、填表单）。这个过滤器在「含大写的纯字母候选」之后紧接着插入一个
-- 全小写版本，于是：
--
--   github  →  GitHub | github
--   githu   →  GitHub（补全）| github（补全）
--   vscode  →  VSCode | vscode
--
-- 只处理**纯 ASCII 字母**：型号（ESP32）、中英混排（X光）、含空格或标点的候选
-- 都不参与，避免产生 esp32 这类无意义的影子候选。
--
-- 位置：必须排在 dedupe_filter **之后**。两者处理方式不冲突（文本不同不会去重），
-- 放在后面可以保证去重先完成，不会因为多出的候选影响判断。
--
-- 注意：rime-lua 要求脚本 **return** 一个函数作为过滤器主体，不能只定义全局 filter。

local function is_ascii_word(text)
  if text == nil then return false end
  return text:match("^[A-Za-z]+$") ~= nil
end

local function filter(input, env)
  for cand in input:iter() do
    yield(cand)

    local text = cand.text
    if is_ascii_word(text) then
      local lower = text:lower()
      if lower ~= text then
        yield(ShadowCandidate(cand, cand.type, lower, cand.comment))
      end
    end
  end
end

return filter
