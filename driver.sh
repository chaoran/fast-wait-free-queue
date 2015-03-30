#!/bin/bash

Z90=1.644854
T90=(
  6.314 2.920 2.353 2.132 2.015 1.943 1.895 1.860 1.833 1.812
  1.796 1.782 1.771 1.761 1.753 1.746 1.740 1.734 1.729 1.725
  1.721 1.717 1.714 1.711 1.708 1.706 1.703 1.701 1.699 .1697
  )

TIMES[0]=$(./fifo | grep Mean | awk '{ print $5 }')
SUM=${TIMES[0]}
printf '#%-2d %.2f\n' 1 ${TIMES[0]}

i=1
while true; do
  TIME=$(./fifo | grep Mean | awk '{ print $5 }')
  TIMES[$i]=$TIME
  SUM=$(echo "$SUM + $TIME" | bc)
  N=$(($i + 1))

  MEAN=$(echo "$SUM / $N" | bc -l)

  STD=0
  for j in "${TIMES[@]}"; do
    STD=$(echo "($j - $MEAN) ^ 2 + $STD" | bc -l)
  done
  STD=$(echo "sqrt ($STD / $i)" | bc -l)

  if (($N < 30)); then
    ERR=$(echo "${T90[$i]} * $STD" | bc -l)
  else
    ERR=$(echo "$Z90 * $STD" | bc -l)
  fi

  PRECISION=$(echo "$ERR / $MEAN" | bc -l)
  C1=$(echo "$MEAN - $ERR" | bc -l)
  C2=$(echo "$MEAN + $ERR" | bc -l)

  printf '#%-2d %.2f %.2f %.4f %.2f %.2f %.2f\n' \
    $N $TIME $MEAN $STD $C1 $C2 $PRECISION

  if (($N > 2 && $(bc <<< "$PRECISION < 0.03") == 1)); then
    break
  else
    i=$N
  fi
done

