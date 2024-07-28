#!/bin/bash

# Start the broker in background
../tbus &
BROKER_PID=$!
sleep 0.1
# Set environment variables
export LD_LIBRARY_PATH=$(pwd)/..
# Get all tests
TESTS=$(ls *_test)
# Run the tests
echo "Running tests..."
for test in $TESTS
do
    echo "Running $test"
    ./$test
    if [ $? -ne 0 ]; then
        echo "Test $test failed"
        kill $BROKER_PID
        exit 1
    fi
done

# Kill the broker
kill $BROKER_PID
echo "All tests passed"
exit 0
