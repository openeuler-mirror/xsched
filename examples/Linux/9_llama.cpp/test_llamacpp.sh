#!/bin/bash
#
# Copyright 2026 KylinSoft  Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# XSched priority scheduling test script
# Defaults to local llama.cpp server, can be overridden by env vars:
#   API_URL="http://host:port" ./this_script.sh
#

set -e

# Support environment variable override, default compatible with llama.cpp standard port
API_URL=${API_URL:-"http://127.0.0.1:8080"}

# Test task parameters
PROMPT_A="Write a very long story."
N_PREDICT_A=1000

PROMPT_B="Write a very long story."
N_PREDICT_B=2000

# Execute task A, return elapsed time (seconds, 2 decimal places)
run_A() {
    local PRIO="$1"
    local START END COST
    START=$(date +%s.%N)
    curl -s -X POST "${API_URL}/completion" \
        -H "Content-Type: application/json" \
        -d "{
            \"prompt\": \"$PROMPT_A\",
            \"n_predict\": $N_PREDICT_A,
            \"priority\": $PRIO,
            \"stream\": false,
            \"temperature\": 0.1
        }" -o /dev/null
    END=$(date +%s.%N)
    COST=$(echo "scale=2; ($END - $START)" | bc)
    echo "$COST"
}

# Execute task B in background, write elapsed time to log file after completion
run_B_bg() {
    local LOG_FILE="$1"
    local START END COST
    START=$(date +%s.%N)
    curl -s -X POST "${API_URL}/completion" \
        -H "Content-Type: application/json" \
        -d "{
            \"prompt\": \"$PROMPT_B\",
            \"n_predict\": $N_PREDICT_B,
            \"priority\": 0,
            \"stream\": false
        }" -o /dev/null
    END=$(date +%s.%N)
    COST=$(echo "scale=2; ($END - $START)" | bc)
    echo "$COST" > "$LOG_FILE"
}

echo "============================================================="
echo "                XSched Priority Scheduling Test"
echo "============================================================="

echo -e "\n[1] Task A Running Alone"
t_A_alone=$(run_A 0)

echo -e "\n[2] Task A and B Running in Parallel"
LOG_B_NORMAL="./b_normal.tmp"
run_B_bg "$LOG_B_NORMAL" &
sleep 0.4
t_A_normal=$(run_A 0)
wait
t_B_normal=$(cat "$LOG_B_NORMAL")

echo -e "\n[3] Task A (High Priority) and B Running in Parallel"
LOG_B_PREEMPT="./b_preempt.tmp"
run_B_bg "$LOG_B_PREEMPT" &
sleep 0.4
t_A_preempt=$(run_A 1)
wait
t_B_preempt=$(cat "$LOG_B_PREEMPT")

echo -e "\n==========================================================================="
echo "                     Time Comparison (Seconds)"
echo "==========================================================================="
echo "Task A Running Alone:                     Task A: ${t_A_alone}s"
echo ""
echo "Task A & Task B Parallel:                 Task A: ${t_A_normal}s        Task B: ${t_B_normal}s"
echo ""
echo "After Setting Task A to High Priority:    Task A: ${t_A_preempt}s       Task B: ${t_B_preempt}s"
echo "==========================================================================="

rm -f "$LOG_B_NORMAL" "$LOG_B_PREEMPT"
