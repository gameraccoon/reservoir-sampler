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

// ReservoirSamplerWeightedStatic implements Algorithm A-ExpJ for reservoir sampling
// https://en.wikipedia.org/wiki/Reservoir_sampling#Algorithm_A-ExpJ
// Objects of the class don't allocate memory on heap (unless stored types allocate data themselves)
template<typename T, size_t SamplesCount, typename WeightType = float, typename URNG = std::mt19937, typename RandType = float>
class ReservoirSamplerWeightedStatic
{
public:
	// C++17 doesn't support std::span, so we can do this instead
	struct ResultSpan
	{
		ResultSpan(T* data, size_t size)
			: data(data)
			, size(size)
		{}

		T* data;
		size_t size;

		T* begin() { return data; }
		const T* begin() const { return data; }
		T* end() { return data + size; }
		const T* end() const { return data + size; }
	};

public:
	template<typename URNG_T = URNG, typename = std::enable_if_t<!std::is_same_v<std::decay_t<URNG_T>, ReservoirSamplerWeightedStatic<T, SamplesCount, WeightType, URNG, RandType>>>>
	explicit ReservoirSamplerWeightedStatic(URNG_T&& rand = std::mt19937{std::random_device{}()})
		: mRand(std::forward<URNG_T>(rand))
	{
		static_assert(std::is_arithmetic_v<WeightType>, "WeightType should be arithmetic type");
		static_assert(std::is_floating_point_v<RandType>, "RandType should be floating point type");
		static_assert(SamplesCount > 0, "SamplesCount should not be zero");
		mPriorityHeap = reinterpret_cast<HeapItem*>(mHeapData);
		mElements = reinterpret_cast<T*>(mData);
	}

	~ReservoirSamplerWeightedStatic()
	{
		reset();
	}

	ReservoirSamplerWeightedStatic(const ReservoirSamplerWeightedStatic& other)
		: mWeightJumpOver(other.mWeightJumpOver)
		, mRand(other.mRand)
		, mUniformDist(other.mUniformDist)
		, mFilledElementsCount(other.mFilledElementsCount)
	{
		mPriorityHeap = reinterpret_cast<HeapItem*>(mHeapData);
		mElements = reinterpret_cast<T*>(mData);

		std::memcpy(mPriorityHeap, other.mPriorityHeap, sizeof(HeapItem)*mFilledElementsCount);

		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			new (mElements + i) T(other.mElements[i]);
		}
	}

	ReservoirSamplerWeightedStatic(ReservoirSamplerWeightedStatic&& other) noexcept
		: mWeightJumpOver(other.mWeightJumpOver)
		, mRand(other.mRand)
		, mUniformDist(other.mUniformDist)
		, mFilledElementsCount(other.mFilledElementsCount)
	{
		mPriorityHeap = reinterpret_cast<HeapItem*>(mHeapData);
		mElements = reinterpret_cast<T*>(mData);

		std::memcpy(mPriorityHeap, other.mPriorityHeap, sizeof(HeapItem)*mFilledElementsCount);

		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			new (mElements + i) T(std::move(other.mElements[i]));
		}

		other.reset();
	}

	ReservoirSamplerWeightedStatic& operator=(const ReservoirSamplerWeightedStatic& other) {
		reset();
		mWeightJumpOver = other.mWeightJumpOver;
		mFilledElementsCount = other.mFilledElementsCount;

		std::memcpy(mPriorityHeap, other.mPriorityHeap, sizeof(HeapItem)*mFilledElementsCount);

		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			new (mElements + i) T(other.mElements[i]);
		}
		return *this;
	}

	ReservoirSamplerWeightedStatic& operator=(ReservoirSamplerWeightedStatic&& other) noexcept {
		reset();
		mWeightJumpOver = other.mWeightJumpOver;
		mFilledElementsCount = other.mFilledElementsCount;

		std::memcpy(mPriorityHeap, other.mPriorityHeap, sizeof(HeapItem)*mFilledElementsCount);

		for (size_t i = 0; i < mFilledElementsCount; ++i)
		{
			new (mElements + i) T(std::move(other.mElements[i]));
		}
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
		mWeightJumpOver = {};
		mFilledElementsCount = 0;
	}

	// optionally use this function in combination with skipNextElement in case creation of an object is expensive
	// you can call skipNextElement every time this method returns false as in these cases the objects will be skipped
	bool willNextElementBeConsidered(WeightType weight) const
	{
		return (mWeightJumpOver - weight) <= 0;
	}

	// optionally use this in combination with willNextElementBeConsidered, refer to the comment above willNextElementBeConsidered
	void skipNextElement(WeightType weight)
	{
		assert(!willNextElementBeConsidered(weight));
		mWeightJumpOver -= static_cast<RandType>(weight);
	}

private:
	struct HeapItem
	{
		RandType priority;
		size_t index;
	};

private:
	template<bool isT, typename... Args>
	void emplace(WeightType weight, Args&&... arguments)
	{
		if (static_cast<RandType>(weight) > static_cast<RandType>(0.0))
		{
			if (mFilledElementsCount < SamplesCount)
			{
				const float rand = mUniformDist(mRand);
				const RandType r = std::pow(rand, static_cast<RandType>(1.0) / static_cast<RandType>(weight));
				insertSorted(r, std::forward<Args>(arguments)...);
				if (mFilledElementsCount == SamplesCount)
				{
					mWeightJumpOver = std::log(mUniformDist(mRand)) / std::log(mPriorityHeap[0].priority);
				}
			}
			else
			{
				mWeightJumpOver -= static_cast<RandType>(weight);
				if (mWeightJumpOver <= static_cast<RandType>(0.0))
				{
					const RandType t = std::pow(mPriorityHeap[0].priority, static_cast<RandType>(weight));
					const RandType r = std::pow(std::uniform_real_distribution<RandType>(t, static_cast<RandType>(1.0))(mRand), static_cast<RandType>(1.0) / static_cast<RandType>(weight));

					insertSortedRemoveFirst<isT>(r, std::forward<Args>(arguments)...);

					mWeightJumpOver = std::log(mUniformDist(mRand)) / std::log(mPriorityHeap[0].priority);
				}
			}
		}
	}

	template<typename... Args>
	void insertSorted([[maybe_unused]]RandType r, Args&&... arguments)
	{
		mPriorityHeap[mFilledElementsCount] = {r, mFilledElementsCount};
		std::push_heap(mPriorityHeap, mPriorityHeap + mFilledElementsCount + 1, [](const HeapItem& a, const HeapItem& b){ return a.priority > b.priority; });

		new (mElements + mFilledElementsCount) T(std::forward<Args>(arguments)...);
		++mFilledElementsCount;
	}

	template<bool isT, typename... Args>
	void insertSortedRemoveFirst(RandType r, Args&&... arguments)
	{
		std::pop_heap(mPriorityHeap, mPriorityHeap + SamplesCount, [](const HeapItem& a, const HeapItem& b){ return a.priority > b.priority; });
		const size_t oldElementIdx = mPriorityHeap[SamplesCount - 1].index;

		mPriorityHeap[SamplesCount - 1] = {r, oldElementIdx};
		std::push_heap(mPriorityHeap, mPriorityHeap + SamplesCount, [](const HeapItem& a, const HeapItem& b){ return a.priority > b.priority; });

		if constexpr (isT && (std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>))
		{
			mElements[oldElementIdx] = std::forward<Args...>(arguments...);
		}
		else if constexpr (std::is_move_assignable_v<T>)
		{
			mElements[oldElementIdx] = T(std::forward<Args>(arguments)...);
		}
		else
		{
			mElements[oldElementIdx].~T();
			new (mElements + oldElementIdx) T(std::forward<Args>(arguments)...);
		}
	}

private:
	RandType mWeightJumpOver {};
	URNG mRand;
	std::uniform_real_distribution<RandType> mUniformDist{static_cast<RandType>(0.0), static_cast<RandType>(1.0)};
	size_t mFilledElementsCount = 0;
	alignas(HeapItem) std::byte mHeapData[sizeof(HeapItem)*SamplesCount];
	alignas(T) std::byte mData[sizeof(T)*SamplesCount];
	HeapItem* mPriorityHeap = nullptr;
	T* mElements = nullptr;
};
