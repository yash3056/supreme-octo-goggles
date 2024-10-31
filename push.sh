#!/bin/bash

# Configuration
BRANCH_NAME="main"  # Replace with your branch name
REMOTE_NAME="origin"            # Replace with your remote name
STEP_SIZE=1000                  # Adjust the step size as needed

# Step 1: List every nth commit (default: every 1000th commit)
step_commits=$(git log --oneline --reverse refs/heads/$BRANCH_NAME | awk "NR % $STEP_SIZE == 0" | awk '{print $1}')

# Step 2: Push each identified commit one by one
echo "Starting incremental push for branch '$BRANCH_NAME' to remote '$REMOTE_NAME' with step size of $STEP_SIZE commits..."

for commit in $step_commits; do
    echo "Pushing commit $commit..."
    git push $REMOTE_NAME +$commit:refs/heads/$BRANCH_NAME

    # Check if the last push succeeded
    if [[ $? -ne 0 ]]; then
        echo "Error pushing commit $commit. Exiting."
        exit 1
    fi
done

# Step 3: Perform a final mirror push to ensure all refs are up-to-date
echo "Performing final mirror push..."
git push $REMOTE_NAME --mirror

echo "Incremental push complete!"
