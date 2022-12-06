// MIT License

// Copyright (c) 2022 Pavel Grebnev

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <optional>
#include <random>
#include <type_traits>

// ReservoirSamplerLinear implements simple reservoir sampleing to get one element out of a stream
// This sampler has linear complexity and ineffective for big streams, it also has potentiol to overflow
// and cause incorrect results or UB if used not carefully, however can be very efficient for small streams
//
// Important: The sum of all weights that go through one instance of this class should fit into WeightType
// Important: Very inefficient with big streams of elements, use other samplers for such cases
template<typename T, typename WeightType = unsigned int, typename URNG = std::mt19937>
class ReservoirSamplerLinear
{
public:
	template<typename URNG_T = URNG, typename = std::enable_if_t<!std::is_same_v<std::decay_t<URNG_T>, ReservoirSamplerLinear<T, WeightType, URNG>>>>
	explicit ReservoirSamplerLinear(URNG_T&& rand = std::mt19937{std::random_device{}()})
		: mRand(std::forward<URNG_T>(rand))
	{
		static_assert(std::is_integral_v<WeightType>, "WeightType should be arithmetic type");
	}

	~ReservoirSamplerLinear() = default;

	ReservoirSamplerLinear(const ReservoirSamplerLinear& other) = default;

	ReservoirSamplerLinear(ReservoirSamplerLinear&& other) noexcept
		: mWeightSum(other.mWeightSum)
		, mSelectedElement(std::move(other.mSelectedElement))
		, mRand(other.mRand)
	{
		other.reset();
	}

	ReservoirSamplerLinear& operator=(const ReservoirSamplerLinear& other) = default;

	ReservoirSamplerLinear& operator=(ReservoirSamplerLinear&& other) noexcept
	{
		mWeightSum = other.mWeightSum;
		mSelectedElement = std::move(other.mSelectedElement);

		other.reset();
		return *this;
	}

	template<typename E, typename = std::enable_if_t<!std::is_lvalue_reference_v<E> && std::is_move_constructible_v<T> && std::is_same_v<std::decay_t<E>, T>>>
	void sampleElement(WeightType weight, E&& element)
	{
		emplace<true>(weight, std::move(element));
	}

	void sampleElement(WeightType weight, const T& element)
	{
		emplace<true>(weight, std::ref(element));
	}

	template<typename... Args>
	void sampleElementEmplace(WeightType weight, Args&&... arguments)
	{
		emplace<false>(weight, std::forward<Args>(arguments)...);
	}

	const std::optional<T>& getResult() const
	{
		return mSelectedElement;
	}

	std::optional<T> consumeResult()
	{
		mWeightSum = {};
		return std::move(mSelectedElement);
	}

	// fully resets the state and cleans all the stored data, allowing to be reused for a new sampling
	void reset()
	{
		mWeightSum = {};
		mSelectedElement = std::nullopt;
	}

private:
	template<bool isT, typename... Args>
	void emplace(WeightType weight, Args&&... arguments)
	{
		if (weight <= 0)
		{
			return;
		}

		mWeightSum += weight;
		if (!mSelectedElement.has_value())
		{
			mSelectedElement.emplace(std::forward<Args>(arguments)...);
		}
		else
		{
			if (static_cast<WeightType>(mRand() % mWeightSum) < weight)
			{
				replaceElement<isT>(std::forward<Args>(arguments)...);
			}
		}
	}

	template<bool isT, typename... Args>
	void replaceElement(Args&&... arguments)
	{
		if constexpr (isT && (std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>))
		{
			mSelectedElement = std::forward<Args...>(arguments...);
		}
		else if constexpr (std::is_move_assignable_v<T> && std::is_move_constructible_v<T>)
		{
			mSelectedElement = T(std::forward<Args>(arguments)...);
		}
		else
		{
			mSelectedElement.emplace(std::forward<Args>(arguments)...);
		}
	}

private:
	WeightType mWeightSum {};
	std::optional<T> mSelectedElement;
	URNG mRand;
};
