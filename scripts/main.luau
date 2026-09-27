const fs = require("@Luwow/fs")

const root = "scripts/filetest"
const testFile = `{root}/test.txt`

print(fs.exists(testFile), fs.type(root)) -- true dir
print(fs.readFile(testFile)) -- hello

const info = fs.stat(testFile)
print(info.type, info.size, info.readonly)

for _, entry in fs.listdir(root) do
    print(entry.name, entry.type)
end

fs.mkdir(`{root}/nested/deep`, true)
fs.copy(testFile, `{root}/copy.txt`)
fs.appendFile(`{root}/copy.txt`, ", world!")
print(fs.readFile(`{root}/copy.txt`)) -- hello, world!

fs.move(`{root}/copy.txt`, `{root}/nested/moved.txt`)
print(fs.exists(`{root}/copy.txt`), fs.exists(`{root}/nested/moved.txt`)) -- false true

const file = fs.open(`{root}/data.bin`, "w+")
file:write("abcdef")
file:write(buffer.fromstring("ghi"))
print(file:tell(), file:size()) -- 9 9
file:seek("set", 2)
print(file:read(3)) -- cde
print(file:read()) -- fghi
file:seek("end", -1)
file:write("!")
file:seek("set")
print(file:read(), file.path) -- abcdefgh! scripts/filetest/data.bin
file:close()
print(file.closed) -- true

const handle = fs.open(testFile)
print(handle:read(2), handle:read()) -- he llo
handle:close()

print(pcall(fs.readFile, `{root}/missing.txt`))
print(pcall(fs.remove, `{root}/nested`))
print(pcall(function() return file:read() end))
print(pcall(function() return fs.open(testFile, "r"):write("x") end))

fs.remove(`{root}/data.bin`)
fs.rmdir(`{root}/nested`, true)
print(fs.exists(`{root}/nested`), fs.readFile(testFile)) -- false hello
