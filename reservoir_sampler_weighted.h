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

template<typename T, typename WeightType = float, typename URBG = std::mt19937, typename RandType = float>
class ReservoirSamplerWeighted
{
public:
	template<typename URBGT = URBG>
	explicit ReservoirSamplerWeighted(size_t samplesCount, URBGT&& rand = std::mt19937{std::random_device{}()})
		: mSamplesCount(samplesCount)
		, mRand(std::forward<URBGT>(rand))
	{
		static_assert(std::is_arithmetic_v<WeightType>, "WeightType should be arithmetic type");
		static_assert(std::is_floating_point_v<RandType>, "RandType should be floating point type");
		assert(samplesCount > 0);
	}

	~ReservoirSamplerWeighted()
	{
		reset();
#ifdef _MSC_VER
		_aligned_free(mData);
#else
		std::free(mData);
#endif
	}

	ReservoirSamplerWeighted(const ReservoirSamplerWeighted& other)
		: mSamplesCount(other.mSamplesCount)
		, mWeightJumpOver(other.mWeightJumpOver)
		, mRand(other.mRand)
		, mUniformDist(other.mUniformDist)
		, mFilledElementsCount(other.mFilledElementsCount)
	{
		if (other.mData)
		{
			allocateData();

			std::memcpy(mPriorityHeap, other.mPriorityHeap, sizeof(HeapItem)*mFilledElementsCount);

			for (size_t i = 0; i < mFilledElementsCount; ++i)
			{
				new (mElements + i) T(other.mElements[i]);
			}
		}
	}

	ReservoirSamplerWeighted(ReservoirSamplerWeighted&& other) noexcept
		: mSamplesCount(other.mSamplesCount)
		, mWeightJumpOver(other.mWeightJumpOver)
		, mRand(other.mRand)
		, mUniformDist(other.mUniformDist)
		, mFilledElementsCount(other.mFilledElementsCount)
		, mData(other.mData)
		, mPriorityHeap(other.mPriorityHeap)
		, mElements(other.mElements)
	{
		other.mWeightJumpOver = {};
		other.mFilledElementsCount = 0;
		other.mData = nullptr;
		other.mPriorityHeap = nullptr;
		other.mElements = nullptr;
	}

	ReservoirSamplerWeighted& operator=(const ReservoirSamplerWeighted&) = delete;
	ReservoirSamplerWeighted& operator=(ReservoirSamplerWeighted&&) noexcept = delete;

	template<typename E, typename = std::enable_if_t<!std::is_lvalue_reference_v<E> && std::is_move_constructible_v<T> && std::is_same_v<std::decay_t<E>, T>>>
	void addElement(WeightType weight, E&& element)
	{
		emplace<true>(weight, std::move(element));
	}

	void addElement(WeightType weight, const T& element)
	{
		emplace<true>(weight, std::ref(element));
	}

	template<typename... Args>
	void emplaceElement(WeightType weight, Args&&... arguments)
	{
		emplace<false>(weight, std::forward<Args>(arguments)...);
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
		mWeightJumpOver = {};
		mFilledElementsCount = 0;
	}

	// optionally use this function in combination with addDummyElement in case creation of an object is expensive
	// you can call addDummyElement every time this method returns false as in these cases the objects will be skipped
	bool willNextBeConsidered(WeightType weight) const
	{
		return (mWeightJumpOver - weight) <= 0;
	}

	// optionally use this in combination with willNextBeConsidered, refer to the comment above willNextBeConsidered
	void addDummyElement(WeightType weight)
	{
		assert(!willNextBeConsidered(weight));
		mWeightJumpOver -= static_cast<RandType>(weight);
	}

	// optionally use if you don't want to delay the memory allocation to the moment of adding the first element
	void allocateData()
	{
		assert(mData == nullptr);
		constexpr size_t alignment = std::max(std::alignment_of_v<HeapItem>, std::alignment_of_v<T>);
		const size_t heapExtent = (sizeof(HeapItem)*mSamplesCount) % std::alignment_of_v<T>;
		const size_t elementsAlignmentGap = heapExtent > 0 ? (std::alignment_of_v<T> - heapExtent) : 0;
		const size_t elementsOffset = sizeof(HeapItem)*mSamplesCount + elementsAlignmentGap;
		const size_t alignedSize = elementsOffset + sizeof(T)*mSamplesCount;

#ifdef _MSC_VER // MSVC doesn't support std::aligned_alloc
		mData = _aligned_malloc(alignedSize, alignment);
#else
		mData = std::aligned_alloc(alignment, alignedSize);
#endif
		mPriorityHeap = reinterpret_cast<HeapItem*>(mData);
		mElements = reinterpret_cast<T*>(static_cast<char*>(mData) + elementsOffset);
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
		if (mData == nullptr)
		{
			allocateData();
		}

		if (static_cast<RandType>(weight) > static_cast<RandType>(0.0))
		{
			if (mFilledElementsCount < mSamplesCount)
			{
				const RandType r = std::pow(mUniformDist(mRand), static_cast<RandType>(1.0) / static_cast<RandType>(weight));
				insertSorted(r, std::forward<Args>(arguments)...);
				if (mFilledElementsCount == mSamplesCount)
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
	void insertSorted(RandType r, Args&&... arguments)
	{
		mPriorityHeap[mFilledElementsCount] = {r, mFilledElementsCount};
		std::push_heap(mPriorityHeap, mPriorityHeap + mFilledElementsCount + 1, [](const HeapItem& a, const HeapItem& b){ return a.priority > b.priority; });

		new (mElements + mFilledElementsCount) T(std::forward<Args>(arguments)...);
		++mFilledElementsCount;
	}

	template<bool isT, typename... Args>
	void insertSortedRemoveFirst(RandType r, Args&&... arguments)
	{
		std::pop_heap(mPriorityHeap, mPriorityHeap + mSamplesCount, [](const HeapItem& a, const HeapItem& b){ return a.priority > b.priority; });
		const size_t oldElementIdx = mPriorityHeap[mSamplesCount - 1].index;

		mPriorityHeap[mSamplesCount - 1] = {r, oldElementIdx};
		std::push_heap(mPriorityHeap, mPriorityHeap + mSamplesCount, [](const HeapItem& a, const HeapItem& b){ return a.priority > b.priority; });

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
	const size_t mSamplesCount;
	RandType mWeightJumpOver {};
	URBG mRand;
	std::uniform_real_distribution<RandType> mUniformDist{static_cast<RandType>(0.0), static_cast<RandType>(1.0)};
	size_t mFilledElementsCount = 0;
	void* mData = nullptr;
	HeapItem* mPriorityHeap = nullptr;
	T* mElements = nullptr;
};
