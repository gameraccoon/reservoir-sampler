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

Here we can have as many lines of user input as we want, and will store only five of them at any time.

```cpp
// print five random words from file (one word per line)
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

If we already have an RNG that will outlive our sampler, we can just pass it by reference instead of creating a new one for the instance.

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

If we know how many samples we want to get at compile time, we can use the non-allocating versions: `ReservoirSamplerStatic` or `ReservoirSamplerWeightedStatic`

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
1. The objects that have heavy logic to even prepare the data to be constructed

The first case is pretty simple, use `sampleElementEmplace` and provide the constructor arguments, the actual construction will take place only if the element is considered to be added.

For the second case you can use a combination of `willNextElementBeConsidered` and `skipNextElement` together with the `sampleElement`/`sampleElementEmplace` that you would use normally

```cpp
ReservoirSamplerWeighted<GoalRecording> recordingsSampler{5};
...
void OnGoal() {
    const float goalWeight = calculateGoalWeight();
    if (recordingsSampler.willNextElementBeConsidered(goalWeight)) {
        // getLastFiveSecondsRecording() performs potentially heavy operations
        recordingsSampler.sampleElement(goalWeight, getLastFiveSecondsRecording());
    } else {
        recordingsSampler.skipNextElement(goalWeight);
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

## Performance

Minimum to no allocations are done depending on the version of the sampler used and the data stored.

As mentioned above, the samplers utilize the algorithms with exponential jumps that greatly reduce calls to random, which are usually very expensive. When using `sampleElementEmplace` it is possible to also reduce amount of object constructions, and it is possible to reduce the costs of preparing data for the elements with techniques of "handling heavy to construct elements" and "skipping iterations" described above.

### Sampling without exponential jumps

And if that's not enough, there's `ReservoirSamplerLinear` that doesn't utilize exponential jumps giving the best performance when sampling from very small streams of elements.

However `ReservoirSamplerLinear` has many limitations:

* can sample only one element (to utilize the most fitting algorithm)
* allows only integer weights (floating point weights require more calculations making `ReservoirSamplerWeightedStatic` more performant even on relatively small streams)
* prone to integer overflows (the sum of weights passed through an instance should fit in the type provided for storing weights)
* very inefficient with big streams compared to other samplers (how "big" depends on your weight type and platform, but generally I would avoid it for streams of more than 100 elements)

## Tests

You can find unit tests for every sampler class here: https://github.com/gameraccoon/reservoir-sampler-tests
