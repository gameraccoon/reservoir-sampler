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
#include <cstring>
#include <random>
#include <type_traits>
#include <vector>

template<typename T, typename URBG = std::mt19937, typename RandType = float>
class ReservoirSampler
{
public:
	template<typename URBGT = URBG>
	explicit ReservoirSampler(size_t samplesCount, URBGT&& rand = std::mt19937{std::random_device{}()})
		: mSamplesCount(samplesCount)
		, mRand(std::forward<URBGT>(rand))
	{
		static_assert(std::is_floating_point_v<RandType>, "RandType should be floating point type");
		assert(samplesCount > 0);
	}

	~ReservoirSampler()
	{
		reset();

#ifdef _MSC_VER
		_aligned_free(mData);
#else
		std::free(mData);
#endif
	}

	ReservoirSampler(const ReservoirSampler& other)
		: mSamplesCount(other.mSamplesCount)
		, mIndexesToJumpOver(other.mIndexesToJumpOver)
		, mWeightJumpOver(other.mWeightJumpOver)
		, mRand(other.mRand)
		, mUniformDist(other.mUniformDist)
		, mFilledElementsCount(other.mFilledElementsCount)
	{
		if (other.mData)
		{
			allocateData();

			for (size_t i = 0; i < mFilledElementsCount; ++i)
			{
				new (mElements + i) T(other.mElements[i]);
			}
		}
	}

	ReservoirSampler(ReservoirSampler&& other) noexcept
		: mSamplesCount(other.mSamplesCount)
		, mIndexesToJumpOver(other.mIndexesToJumpOver)
		, mWeightJumpOver(other.mWeightJumpOver)
		, mRand(other.mRand)
		, mUniformDist(other.mUniformDist)
		, mFilledElementsCount(other.mFilledElementsCount)
		, mElements(other.mElements)
		, mData(other.mData)
	{
		other.mIndexesToJumpOver = 0;
		other.mWeightJumpOver = {};
		other.mFilledElementsCount = 0;
		other.mElements = nullptr;
		other.mData = nullptr;
	}

	ReservoirSampler& operator=(const ReservoirSampler&) = delete;
	ReservoirSampler& operator=(ReservoirSampler&&) noexcept = delete;

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

	// optionally use in case in combination with jumpAhead to skip elements that are not going to be considered
	// in case iterating over these elements can be skipped
	size_t getNextElementsSkippedNumber()
	{
		return mIndexesToJumpOver;
	}

	// optionally use this in combination with getNextElementsSkippedNumber,
	// refer to the comment above getNextElementsSkippedNumber
	void jumpAhead(size_t elementsToJumpOver)
	{
		assert(elementsToJumpOver <= mIndexesToJumpOver);
		mIndexesToJumpOver -= elementsToJumpOver;
	}

	// optionally use if you don't want to delay the memory allocation to the moment of adding the first element
	void allocateData()
	{
		assert(mData == nullptr);
#ifdef _MSC_VER // MSVC doesn't support std::aligned_alloc
		mData = _aligned_malloc(sizeof(T) * mSamplesCount, std::alignment_of_v<T>);
#else
		mData = std::aligned_alloc(std::alignment_of_v<T>, sizeof(T)*mSamplesCount);
#endif
		mElements = reinterpret_cast<T*>(mData);
	}

private:
	template<bool isT, typename... Args>
	void emplace(Args&&... arguments)
	{
		if (mData == nullptr)
		{
			allocateData();
		}

		if (mFilledElementsCount < mSamplesCount)
		{
			insertInitial(std::forward<Args>(arguments)...);

			if (mFilledElementsCount == mSamplesCount)
			{
				mWeightJumpOver = std::exp(std::log(mUniformDist(mRand)) / mSamplesCount);
				mIndexesToJumpOver = static_cast<size_t>(std::floor(std::log(mUniformDist(mRand))/std::log(static_cast<RandType>(1.0) - mWeightJumpOver)));
			}
		}
		else
		{
			if (mIndexesToJumpOver == 0)
			{
				replaceElement<isT>(std::forward<Args>(arguments)...);

				mWeightJumpOver *= std::exp(std::log(mUniformDist(mRand)) / mSamplesCount);
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
		const size_t pos = mRand() % mSamplesCount;
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
	const size_t mSamplesCount;
	size_t mIndexesToJumpOver = 0;
	RandType mWeightJumpOver {};
	URBG mRand;
	std::uniform_real_distribution<RandType> mUniformDist{static_cast<RandType>(0.0), static_cast<RandType>(1.0)};
	size_t mFilledElementsCount = 0;
	T* mElements = nullptr;
	void* mData = nullptr;
};
