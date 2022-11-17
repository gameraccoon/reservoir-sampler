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

// ReservoirSamplerStatic implements Algorithm L for reservoir sampling
// https://en.wikipedia.org/wiki/Reservoir_sampling#Optimal:_Algorithm_L
// Objects of the class don't allocate memory on heap (unless stored types allocate data themselves)
template<typename T, size_t SamplesCount, typename URNG = std::mt19937, typename RandType = float>
class ReservoirSamplerStatic
{
public:
	// C++17 doesn't support std::span, so we can do this instead
	struct ResultSpan
	{
		T* data;
		size_t size;

		T* begin() { return data; }
		const T* begin() const { return data; }
		T* end() { return data + size; }
		const T* end() const { return data + size; }
	};

public:
	template<typename URNG_T = URNG, typename = std::enable_if_t<!std::is_same_v<std::decay_t<URNG_T>, ReservoirSamplerStatic<T, SamplesCount, URNG, RandType>>>>
	explicit ReservoirSamplerStatic(URNG_T&& rand = std::mt19937{std::random_device{}()})
		: mRand(std::forward<URNG_T>(rand))
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

	ReservoirSamplerStatic& operator=(const ReservoirSamplerStatic& other)
	{
		reset();
		mIndexesToJumpOver = other.mIndexesToJumpOver;
		mWeightJumpOver = other.mWeightJumpOver;
		mFilledElementsCount = other.mFilledElementsCount;

		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			new (mElements + i) T(other.mElements[i]);
		}
		return *this;
	}

	ReservoirSamplerStatic& operator=(ReservoirSamplerStatic&& other) noexcept
	{
		reset();
		mIndexesToJumpOver = other.mIndexesToJumpOver;
		mWeightJumpOver = other.mWeightJumpOver;
		mFilledElementsCount = other.mFilledElementsCount;

		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			new (mElements + i) T(std::move(other.mElements[i]));
		}
		other.reset();
		return *this;
	}

	template<typename E, typename = std::enable_if_t<!std::is_lvalue_reference_v<E> && std::is_move_constructible_v<T> && std::is_same_v<std::decay_t<E>, T>>>
	void sampleElement(E&& element)
	{
		emplace<true>(std::move(element));
	}

	void sampleElement(const T& element)
	{
		emplace<true>(std::ref(element));
	}

	template<typename... Args>
	void sampleElementEmplace(Args&&... arguments)
	{
		emplace<false>(std::forward<Args>(arguments)...);
	}

	ResultSpan getResult() const
	{
		return ResultSpan(mElements, mFilledElementsCount);
	}

	std::vector<T> consumeResult()
	{
		std::vector<T> result;
		result.reserve(mFilledElementsCount);
		std::move(mElements, mElements + mFilledElementsCount, std::back_inserter(result));

		reset();

		return result;
	}

	size_t getResultSize() const { return mFilledElementsCount; }

	// outRawData should point to a C-array with enough memory to fit getResultSize() elements
	void consumeResultTo(T* outRawData)
	{
		std::move(mElements, mElements + mFilledElementsCount, outRawData);
		reset();
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

	// optionally use this function in combination with skipNextElement in case creation of an object is expensive
	// you can call skipNextElement every time this method returns false as in these cases the objects will be skipped
	bool willNextElementBeConsidered() const
	{
		return mIndexesToJumpOver == 0;
	}

	// optionally use this in combination with willNextElementBeConsidered, refer to the comment above willNextElementBeConsidered
	void skipNextElement()
	{
		assert(!willNextElementBeConsidered());
		--mIndexesToJumpOver;
	}

	// optionally use in case in combination with jumpAhead to skip elements that are not going to be considered
	// in case iterating over these elements can be skipped
	size_t getNextSkippedElementsCount()
	{
		return mIndexesToJumpOver;
	}

	// optionally use this in combination with getNextSkippedElementsCount,
	// refer to the comment above getNextSkippedElementsCount
	void jumpAhead(size_t elementsToJumpOver)
	{
		assert(elementsToJumpOver <= mIndexesToJumpOver);
		mIndexesToJumpOver -= elementsToJumpOver;
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
	URNG mRand;
	std::uniform_real_distribution<RandType> mUniformDist{static_cast<RandType>(0.0), static_cast<RandType>(1.0)};
	size_t mFilledElementsCount = 0;
	T* mElements = nullptr;
	alignas(T) std::byte mData[sizeof(T)*SamplesCount];
};
