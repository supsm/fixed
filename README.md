# fixed
Simple single header C++20 fixed point arithmetic with non-overflowing multiplication/division.  
Modules are not supported, this uses the simple old school #include.  

Multiplication and division, when `fast_multdiv` template parameter is false, use long multiplication and emulated division on multiple integers. The result will obviously still overflow if it cannot be represented by the underlying type.  
This also means division on devices without the hardware instruction will be equally as fast. In testing emulated division performed equal to multi-word division using division instructions.  

Custom types may be used as the underlying type if and only if:
- numeric_limits specialization is defined
- radix == 2
- is integer
- is bounded

This means "big int" libraries may be used only if their sizes are fixed at compile time.  

Copy and move are based on the underlying type, which is trivial for integers. Most likely you want to pass objects around by value rather than reference, as you would do for integers.  

## Example
```c++
#include "fixed.h"
// 16 bits for both integer and fractional part
using fixed = supsm::fixed<uint32_t, 16>;

int main()
{
	fixed f = 1; // create fixed point with value 1
	constexpr fixed f2(1, 4); // 1/2^4 = 1/16
	constexpr fixed f3 = 13;
	// cast to underlying integer type, truncating fractional part
	// conversions must be explicit
	// all operations and conversions are constexpr
	constexpr uint32_t result = static_cast<uint32_t>(f / f2);
	supsm::fixed<int32_t, 16> neg(-5, 6); // -5/2^6. Negatives use two's complement following underlying type
	f <<= 6; // bitshift acts as multiplication by 2^6
	// divide f by 31 (fixed point), then convert to floating point
	float fl = static_cast<float>(f / 31);
}
```

## Limitations
- Fixed point operations must be performed on exactly the same type. Adding `fixed<int64_t, 16>` and `fixed<int32_t, 16>` or `fixed<int64_t, 24>` is not possible directly
- Functions other than the basic arithmetic, logic, and comparison operators (e.g. sqrt, log) are not provided and must be implemented by the user
  - Example sqrt implementation:
    ```c++
    constexpr fixed sqrt(fixed x)
    {
    	fixed est = x, last_est;
    	do
    	{
    		last_est = est;
    		est += x / est;
    		est >>= 1;
    	}
    	while (est != last_est);
    	return est;
    }
    ```
