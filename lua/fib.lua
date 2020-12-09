function fib(n)
  if n < 2 then
    return n
  else
    return fib(n-1) + fib(n-2)
  end
end

local n = 20
if #arg >= 1 then
   n = tonumber(arg[1])
end
print(fib(n))
