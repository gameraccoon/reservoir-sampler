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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <random>
#include <type_traits>
#include <vector>

template<typename T, size_t SamplesCount, typename URBG = std::mt19937, typename RandType = float>
class ReservoirSamplerStatic
{
public:
	template<typename URBGT = URBG, typename = std::enable_if_t<!std::is_same_v<std::decay_t<URBGT>, ReservoirSamplerStatic<T, SamplesCount, URBG, RandType>>>>
	explicit ReservoirSamplerStatic(URBGT&& rand = std::mt19937{std::random_device{}()})
		: mRand(std::forward<URBGT>(rand))
	{
		static_assert(std::is_floating_point_v<RandType>, "RandType should be floating point type");
		static_assert(SamplesCount > 0, "SamplesCount should not be zero");
		mElements = reinterpret_cast<T*>(mData);
	}

	~ReservoirSamplerStatic()
	{
		reset();
	}

	ReservoirSamplerStatic(const ReservoirSamplerStatic& other)
		: mIndexesToJumpOver(other.mIndexesToJumpOver)
		, mWeightJumpOver(other.mWeightJumpOver)
		, mRand(other.mRand)
		, mUniformDist(other.mUniformDist)
		, mFilledElementsCount(other.mFilledElementsCount)
	{
		mElements = reinterpret_cast<T*>(mData);
		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			new (mElements + i) T(other.mElements[i]);
		}
	}

	ReservoirSamplerStatic(ReservoirSamplerStatic&& other) noexcept
		: mIndexesToJumpOver(other.mIndexesToJumpOver)
		, mWeightJumpOver(other.mWeightJumpOver)
		, mRand(other.mRand)
		, mUniformDist(other.mUniformDist)
		, mFilledElementsCount(other.mFilledElementsCount)
	{
		mElements = reinterpret_cast<T*>(mData);
		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			new (mElements + i) T(std::move(other.mElements[i]));
		}
		other.reset();
	}

	ReservoirSamplerStatic& operator=(const ReservoirSamplerStatic&) = delete;
	ReservoirSamplerStatic& operator=(ReservoirSamplerStatic&&) noexcept = delete;

	template<typename E, typename = std::enable_if_t<!std::is_lvalue_reference_v<E> && std::is_move_constructible_v<T> && std::is_same_v<std::decay_t<E>, T>>>
	void addElement(E&& element)
	{
		emplace<true>(std::move(element));
	}

	void addElement(const T& element)
	{
		emplace<true>(std::ref(element));
	}

	template<typename... Args>
	void emplaceElement(Args&&... arguments)
	{
		emplace<false>(std::forward<Args>(arguments)...);
	}

	std::pair<const T*, size_t> getResult() const
	{
		return std::make_pair(mElements, mFilledElementsCount);
	}

	std::vector<T> consumeResult()
	{
		std::vector<T> result;
		result.reserve(mFilledElementsCount);
		std::move(mElements, mElements + mFilledElementsCount, std::back_inserter(result));

		reset();

		return result;
	}

	// fully resets the state and cleans all the stored data, allowing to be reused for a new sampling
	void reset()
	{
		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			mElements[i].~T();
		}
		mIndexesToJumpOver = 0;
		mWeightJumpOver = {};
		mFilledElementsCount = 0;
	}

	// optionally use this function in combination with addDummyElement in case creation of an object is expensive
	// you can call addDummyElement every time this method returns false as in these cases the objects will be skipped
	bool willNextBeConsidered() const
	{
		return mIndexesToJumpOver == 0;
	}

	// optionally use this in combination with willNextBeConsidered, refer to the comment above willNextBeConsidered
	void addDummyElement()
	{
		assert(!willNextBeConsidered());
		--mIndexesToJumpOver;
	}

private:
	template<bool isT, typename... Args>
	void emplace(Args&&... arguments)
	{
		if (mFilledElementsCount < SamplesCount)
		{
			insertInitial(std::forward<Args>(arguments)...);

			if (mFilledElementsCount == SamplesCount)
			{
				mWeightJumpOver = std::exp(std::log(mUniformDist(mRand)) / SamplesCount);
				mIndexesToJumpOver = static_cast<size_t>(std::floor(std::log(mUniformDist(mRand))/std::log(static_cast<RandType>(1.0) - mWeightJumpOver)));
			}
		}
		else
		{
			if (mIndexesToJumpOver == 0)
			{
				replaceElement<isT>(std::forward<Args>(arguments)...);

				mWeightJumpOver *= std::exp(std::log(mUniformDist(mRand)) / SamplesCount);
				mIndexesToJumpOver += static_cast<size_t>(std::floor(std::log(mUniformDist(mRand))/std::log(static_cast<RandType>(1.0) - mWeightJumpOver)));
			}
			else
			{
				--mIndexesToJumpOver;
			}
		}
	}

	template<typename... Args>
	void insertInitial(Args&&... arguments)
	{
		new (mElements + mFilledElementsCount) T(std::forward<Args>(arguments)...);
		++mFilledElementsCount;
	}

	template<bool isT, typename... Args>
	void replaceElement(Args&&... arguments)
	{
		const size_t pos = mRand() % SamplesCount;
		if constexpr (isT && (std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>))
		{
			mElements[pos] = std::forward<Args...>(arguments...);
		}
		else if constexpr (std::is_move_assignable_v<T>)
		{
			mElements[pos] = T(std::forward<Args>(arguments)...);
		}
		else
		{
			mElements[pos].~T();
			new (mElements + pos) T(std::forward<Args>(arguments)...);
		}
	}

private:
	size_t mIndexesToJumpOver = 0;
	RandType mWeightJumpOver {};
	URBG mRand;
	std::uniform_real_distribution<RandType> mUniformDist{static_cast<RandType>(0.0), static_cast<RandType>(1.0)};
	size_t mFilledElementsCount = 0;
	T* mElements = nullptr;
	alignas(T) std::byte mData[sizeof(T)*SamplesCount];
};
