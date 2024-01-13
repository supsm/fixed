/*
MIT License

Copyright (c) 2024 supsm

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include <compare>
#include <concepts>
#include <limits>
#include <type_traits>

namespace supsm
{
	namespace detail
	{
		template<typename T, typename T2>
		concept integer_or_T = std::integral<T> || std::same_as<T, T2>;
	}
	
	// simple fixed point type
	// overflow behavior follows T
	// @tparam T  internal type to store data in. determines total number of bits available and signed/unsigned-ness
	// @tparam scale_bits  scale factor, in bits. 0 = no scaling (i.e. integer)
	// @tparam fast_multdiv  true for fast, frequently overflowing multiplication/division (top scale_bits bits needs to always be empty); false for slow, multiword mult/div that only overflows when the output cannot be represented using T
	// @tparam multdiv_cast_type  iff fast_multdiv, specifies type to cast operands as. Ideally has scale_bits more bits than T and same signedness
	template<typename T, std::size_t scale_bits, bool fast_multdiv = false, typename multdiv_cast_type = T>
	class fixed
	{
		// no funny business allowed
		static_assert(std::numeric_limits<T>::radix == 2);
		static_assert(std::numeric_limits<T>::is_integer);
		static_assert(std::numeric_limits<T>::is_bounded);
		
		public:
		using internal_type = T;
		
		T raw_data = 0;
		
		// create an empty fixed point number (value 0)
		constexpr fixed() = default;
		// store a given integer value as fixed point
		constexpr fixed(detail::integer_or_T<T> auto int_val) : raw_data(T(int_val) << scale_bits) {}
		// store an integer value with scale multiple:
		// int_val * 2^(-scale)
		constexpr fixed(detail::integer_or_T<T> auto int_val, std::size_t scale) : raw_data(T(int_val) << (scale_bits - scale)) {}
		
		// convert to int, truncating fractional part
		template<std::integral T2>
		constexpr explicit operator T2() const
		{
			// NOTE:
			// T2 may not fit raw_data, so we must
			// cast after the shift. Shifting right
			// should have no issues with T.
			return T2(raw_data) >> scale_bits;
		}
		constexpr explicit operator float() const
		{
			return float(raw_data) / (T(1) << scale_bits);
		}
		constexpr explicit operator double() const
		{
			return double(raw_data) / (T(1) << scale_bits);
		}
		
		constexpr fixed operator+() const { fixed result; result.raw_data = +raw_data; return result; }
		constexpr fixed operator-() const { fixed result; result.raw_data = -raw_data; return result; }
		constexpr fixed operator+(auto other) const { fixed result = *this; result += other; return result; }
		constexpr fixed operator-(auto other) const { fixed result = *this; result -= other; return result; }
		constexpr fixed operator*(auto other) const { fixed result = *this; result *= other; return result; }
		constexpr fixed operator/(auto other) const { fixed result = *this; result /= other; return result; }
		constexpr fixed operator%(auto other) const { fixed result = *this; result %= other; return result; }
		
		constexpr fixed operator~() const { fixed result; result.raw_data = ~raw_data; return result; }
		constexpr fixed operator&(auto other) const { fixed result = *this; result &= other; return result; }
		constexpr fixed operator|(auto other) const { fixed result = *this; result |= other; return result; }
		constexpr fixed operator^(auto other) const { fixed result = *this; result ^= other; return result; }
		constexpr fixed operator<<(detail::integer_or_T<T> auto amt) const { fixed result = *this; result <<= amt; return result; }
		constexpr fixed operator>>(detail::integer_or_T<T> auto amt) const { fixed result = *this; result >>= amt; return result; }
		
		constexpr fixed& operator+=(const fixed& other) { raw_data += other.raw_data; return *this; }
		constexpr fixed& operator-=(const fixed& other) { raw_data -= other.raw_data; return *this; }
		// if fast_multdiv is false, uses half-size long multiplication
		//   such that overflow only occurs if the result does not fit
		// otherwise multiplies `raw_data` members, which may cause overflow
		constexpr fixed& operator*=(const fixed& other)
		{
			if constexpr (!fast_multdiv)
			{
				using UT = std::make_unsigned_t<T>;
				// divide total bits by 2, rounding up
				constexpr auto low_bits_num = std::numeric_limits<UT>::digits / 2;
				constexpr auto bits_num = std::numeric_limits<UT>::digits;
				auto high_bits = [low_bits_num](auto x) { return static_cast<UT>(x) >> low_bits_num; };
				auto low_bits = [low_bits_num](auto x) { return static_cast<UT>(x) & ((UT(1) << low_bits_num) - 1); };
		
				UT a = raw_data, b = other.raw_data;
				bool negate;
				if constexpr (std::is_signed_v<T>)
				{
					// abs then restore sign at end
					bool neg_a = a >> (bits_num - 1);
					if (neg_a) { a = -a; }
					bool neg_b = b >> (bits_num - 1);
					if (neg_b) { b = -b; }
					negate = neg_a != neg_b; // boolean xor
				}
		
				// long multiplication
				//       H1 L1
				// x     H2 L2
				// -----------
				//        L1L2
				//     H1L2  |
				//     H2L1  |
				//  H1H2  |  |
				//  E1L2  |  |
				//  L1E2  |  |
				// R3 R2 R1 R0
				const UT L1_L2 = low_bits(a) * low_bits(b);
				// lowest bits of result
				const UT result_0 = low_bits(L1_L2);
				// keep H1L2 and L1H2 separate to prevent overflow
				const UT H1_L2_C = high_bits(a) * low_bits(b) + high_bits(L1_L2);
				// we only add low bits of H1_L2_C as a carry so no risk of overflow
				const UT L1_H2_C = low_bits(a) * high_bits(b) + low_bits(H1_L2_C);
				const UT result_1 = low_bits(L1_H2_C);
				// very narrowly still guaranteed overflow-free
				// (n-1)^2 + 2n = n^2-1
				const UT H1_H2_C = high_bits(a) * high_bits(b) + high_bits(H1_L2_C) + high_bits(L1_H2_C);
				UT result_2, result_3;
				result_2 = low_bits(H1_H2_C);
				result_3 = high_bits(H1_H2_C);
		
				UT result_low = result_0 | (result_1 << low_bits_num);
				UT result_high = result_2 | (result_3 << low_bits_num);
		
				raw_data = static_cast<T>(((result_high & ((UT(1) << scale_bits) - 1)) << (bits_num - scale_bits)) | (result_low >> scale_bits));
				if constexpr (std::is_signed_v<T>)
				{
					if (negate)
					{
						raw_data = -raw_data;
					}
				}
			}
			else
			{
				raw_data = (static_cast<multdiv_cast_type>(raw_data) * static_cast<multdiv_cast_type>(other.raw_data)) >> scale_bits;
			}
			return *this;
		}
		// if multiplying by an integer, we can multiply directly
		// instead of constructing a fixed point number
		// then (potentially) using long multiplication
		constexpr fixed& operator*=(detail::integer_or_T<T> auto other)
		{
			raw_data *= other;
			return *this;
		}
		constexpr fixed& operator/=(const fixed& other)
		{
			if constexpr (!fast_multdiv)
			{
				using UT = std::make_unsigned_t<T>;
				constexpr auto bits_num = std::numeric_limits<UT>::digits;
				UT a = raw_data, b = other.raw_data;
				bool negate;
				if constexpr (std::is_signed_v<T>)
				{
					// abs then restore sign at end
					bool neg_a = a >> (bits_num - 1);
					if (neg_a) { a = -a; }
					bool neg_b = b >> (bits_num - 1);
					if (neg_b) { b = -b; }
					negate = neg_a != neg_b; // boolean xor
				}
				UT div_high = (scale_bits == 0 ? 0 : a >> (bits_num - scale_bits));
				UT div_low = a << scale_bits;
				// "Hardware Shift-and-Subtract Long Division" from Hacker's Delight
				for (std::size_t i = 1; i <= bits_num; i++)
				{
					UT t = -(div_high >> (bits_num - 1)); // All 1’s if highest bit is set
					// shift div left 1 bit
					div_high = (div_high << 1) | (div_low >> (bits_num - 1));
					div_low <<= 1;
					if ((div_high | t) >= b)
					{
						div_high -= b;
						div_low++;
					}
				}
				if constexpr (std::is_signed_v<T>)
				{
					if (negate)
					{
						div_low = -div_low;
					}
				}
				raw_data = static_cast<T>(div_low);
			}
			else
			{
				raw_data = (static_cast<multdiv_cast_type>(raw_data) << scale_bits) / static_cast<multdiv_cast_type>(other.raw_data);
			}
			return *this;
		}
		// if dividing by an integer, we can divide directly
		constexpr fixed& operator/=(detail::integer_or_T<T> auto other)
		{
			raw_data /= other;
			return *this;
		}
		// modulus is straightforward and can be implemented
		// by just applying to raw_data
		constexpr fixed& operator%=(const fixed& other)
		{
			raw_data %= other.raw_data;
			return *this;
		}
		
		constexpr fixed& operator&=(const fixed& other) { raw_data &= other.raw_data; return *this; }
		constexpr fixed& operator|=(const fixed& other) { raw_data |= other.raw_data; return *this; }
		constexpr fixed& operator^=(const fixed& other) { raw_data ^= other.raw_data; return *this; }
		constexpr fixed& operator<<=(detail::integer_or_T<T> auto amt) { raw_data <<= amt; return *this; }
		constexpr fixed& operator>>=(detail::integer_or_T<T> auto amt) { raw_data >>= amt; return *this; }
		
		constexpr friend fixed operator+(detail::integer_or_T<T> auto left, const fixed& right) { return fixed(left) + right; }
		constexpr friend fixed operator-(detail::integer_or_T<T> auto left, const fixed& right) { return fixed(left) - right; }
		// multiplication is commutative and can be simplified
		// easily for integers, unlike division and modulus
		constexpr friend fixed operator*(detail::integer_or_T<T> auto left, const fixed& right) { fixed result = right; result.raw_data *= left; return result; }
		constexpr friend fixed operator/(detail::integer_or_T<T> auto left, const fixed& right) { return fixed(left) / right; }
		constexpr friend fixed operator%(detail::integer_or_T<T> auto left, const fixed& right) { return fixed(left) % right; }
		
		constexpr friend fixed operator&(detail::integer_or_T<T> auto left, const fixed& right) { return fixed(left) & right; }
		constexpr friend fixed operator|(detail::integer_or_T<T> auto left, const fixed& right) { return fixed(left) | right; }
		constexpr friend fixed operator^(detail::integer_or_T<T> auto left, const fixed& right) { return fixed(left) ^ right; }
		
		constexpr std::strong_ordering operator<=>(const fixed& other) const { return raw_data <=> other.raw_data; }
		constexpr bool operator==(const fixed& other) const = default;
	};
}

namespace std
{
	template<typename T, std::size_t scale_bits, bool fast_multdiv, typename multdiv_cast_type>
	class numeric_limits<::supsm::fixed<T, scale_bits, fast_multdiv, multdiv_cast_type>>
	{
		using fixed_type = ::supsm::fixed<T, scale_bits, fast_multdiv, multdiv_cast_type>;
		static constexpr double log10_2 = 0.301029995663981195; // std::log10 not constexpr until C++26
	public:
		static constexpr bool is_specialized = true;
		static constexpr bool is_signed = numeric_limits<T>::is_signed;
		static constexpr bool is_integer = false;
		static constexpr bool is_exact = numeric_limits<T>::is_exact;
		static constexpr bool has_infinity = false;
		static constexpr bool has_quiet_NaN = false;
		static constexpr bool has_signaling_NaN = false;
		static constexpr bool round_style = (fast_multdiv ? numeric_limits<T>::round_style : round_toward_zero);
		static constexpr bool is_iec559 = false;
		static constexpr bool is_bounded = true;
		static constexpr bool is_modulo = numeric_limits<T>::is_modulo;
		static constexpr bool digits = numeric_limits<T>::digits;
		static constexpr int digits10 = digits * log10_2;
		static constexpr int radix = 2;
		static constexpr int min_exponent = scale_bits;
		static constexpr int min_exponent10 = scale_bits * log10_2;
		static constexpr int max_exponent = numeric_limits<T>::digits - scale_bits;
		static constexpr int max_exponent10 = (numeric_limits<T>::digits - scale_bits) * log10_2;
		static constexpr int traps = numeric_limits<T>::traps;
		static constexpr int tinyness_before = false;
		static constexpr fixed_type min() { fixed_type m; m.raw_data = numeric_limits<T>::min(); return m; }
		static constexpr fixed_type lowest() { fixed_type m; m.raw_data = numeric_limits<T>::lowest(); return m; }
		static constexpr fixed_type max() { fixed_type m; m.raw_data = numeric_limits<T>::max(); return m; }
		static constexpr fixed_type epsilon() { return fixed_type(1, scale_bits); }
		static constexpr fixed_type round_error() { return 1; }
	};
}
