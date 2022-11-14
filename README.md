[![MIT License](https://img.shields.io/apm/l/atomic-design-ui.svg?)](https://github.com/gameraccoon/hide-and-seek/blob/develop/License.txt)  
[![Ubuntu GCC - build and run unit-tests](https://github.com/gameraccoon/reservoir-sampler-tests/actions/workflows/run-tests-ubuntu-gcc.yml/badge.svg?branch=main)](https://github.com/gameraccoon/reservoir-sampler-tests/actions/workflows/run-tests-ubuntu-gcc.yml) [![Windows MSVC - build and run unit tests](https://github.com/gameraccoon/reservoir-sampler-tests/actions/workflows/run-tests-windows-msvc.yml/badge.svg?branch=main)](https://github.com/gameraccoon/reservoir-sampler-tests/actions/workflows/run-tests-windows-msvc.yml)

Header-only C++17 library for performing [Reservoir Sampling](https://en.wikipedia.org/wiki/Reservoir_sampling).

Features:
* Fast and lightweight
* Minimum allocations (can be one or zero if stored types don't allocate, see examples below)
* Implementation with exponential jumps allowing to construct only the objects that will be considered (see examples below)
* Support for non-copyable and non-moveable types
* Support for weighted sampling
* For `k` is amount of final samples (constant from the moment of construction) and `n` is amount items in the stream:
  * space is `O(k)`
  * amount of constructions and random calls `O(k + k*log(n/k))`

## Examples of use
### Simple

Here we can have as many lines of user input as we want, and will store only five of them at a time.

```cpp
// print five random words from file
ReservoirSampler<std::string> randomWordsSampler{5};

std::ifstream infile("10000_words.txt");
std::string input;
while (infile >> input) {
    randomWordsSampler.sampleElement(input);
}

for (const std::string& word : randomWordsSampler.getResult()) {
    std::cout << word << "\n";
}
```

### Weighted

If we want to make a weighted sampling, so we can specify what are the relative chances of each element to appear in the final sample compared to other elements, we can use weighted versions of the sampler: `ReservoirSamplerWeighted` and `ReservoirSamplerWeightedStatic`.

The type of the weight can be set as a template parameter and can be integer as well (`float` by default).

```cpp
ReservoirSamplerWeighted<GoalRecording> recordingsSampler{5};
...
void OnGoalScored() {
    recordingsSampler.sampleElement(calculateGoalWeight(), getLastFiveSecondsRecording());
}
...
void OnMatchEnded() {
    replayRecordings(recordingsSampler.consumeResult());
}
```

### Store random by reference

If we already have a RNG that will outlive our sampler, then we can just pass it by reference to avoid storing a full copy of the RNG in the instance.

```cpp
// print five random words from file
std::mt19937 rand{std::random_device{}()};
ReservoirSampler<std::string> randomWordsSampler{5, rand};

std::ifstream infile("10000_words.txt");
std::string input;
while (infile >> input) {
    randomWordsSampler.sampleElement(input);
}
...
```

### Static (non allocating)

If we know at compile time how many samples we want to get, then we can use non-allocating versions `ReservoirSamplerStatic` or `ReservoirSamplerWeightedStatic`

```cpp
// print five random words from file
ReservoirSamplerStatic<std::string, 5> randomWordsSampler;

std::ifstream infile("10000_words.txt");
std::string input;
while (infile >> input) {
    randomWordsSampler.sampleElement(input);
}
...
```

### Handling heavy to construct elements

There are two cases that are covered:
1. The objects that have heavy constructors
1. The objects that have heavy logic to prepare data to be constructed

The first case is pretty simple, use `sampleElementEmplace` and provide the constructor arguments, the actual construction will take place only if the element is considered to be added.

For the second case you can use a combination of `willBeConsidered` and `addDummy` together with the `sampleElement`/`sampleElementEmplace` that you would use normally

```cpp
ReservoirSamplerWeighted<GoalRecording> recordingsSampler{5};
...
void OnGoal() {
    const float goalWeight = calculateGoalWeight();
    if (recordingsSampler.willNextElementBeConsidered(goalWeight)) {
        // getLastFiveSecondsRecording() performs potentially heavy operations
        recordingsSampler.sampleElement(goalWeight, getLastFiveSecondsRecording());
    } else {
        recordingsSampler.skipNextElement();
    }
}
...
```

### Skipping iterations

When using non-weighted samplers we can jump ahead some iterations when we know they won't be considered.  
This can be a bit more efficient on big streams when we iterate over batches of elements instead of receiving individual elements one at a time.

```cpp
ReservoirSampler<MyElement> recordingsSampler{5};
...
void OnBatchReceived(const std::vector<MyElement>& batch) {
    for (size_t i = 0; i < batch.size(); ++i) {
        sampler.sampleElement(batch[i]);
        const size_t skipAmount = std::min(sampler.getNextSkippedElementsCount(), batch.size() - i - 1);
        i += skipAmount;
        sampler.jumpAhead(skipAmount);
    }
}
...
```

## Tests

You can find unit tests for every sampler class here: https://github.com/gameraccoon/reservoir-sampler-tests
