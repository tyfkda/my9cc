function primep(x)
  for i = 2, x do
    if i * i > x then
      break
    end
    if x % i == 0 then
      return false
    end
  end
  return true
end

function enum_prime(n)
  for x = 2, n do
    if primep(x) then
      io.write(x)
      io.write("\n")
    end
  end
end

enum_prime(100)
