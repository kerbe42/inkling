#!/bin/sh
# Builds and runs every host game test suite (address + undefined-behaviour sanitizers). Usage: run_game_tests.sh [quick|full]
set -e
cd "$(dirname "$0")/../../.."
mode=${1:-quick}
out=${TMPDIR:-/tmp}/scribe-game-tests
mkdir -p "$out"
G=device/app/games
build() { gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I device/app -I $G -o "$out/$1" $G/tests/stub_$1.c $G/tests/test_$1.c $2 -lm; }
build a "$G/dots.c $G/gomoku.c $G/reversi.c $G/battleship.c $G/nim.c $G/lightsout.c"
build b "$G/dict_words.c $G/hangman.c $G/minesweeper.c $G/sudoku.c $G/memory.c $G/wordsearch.c $G/codebreaker.c $G/sliding.c"
build c "$G/wordsearch.c $G/maze.c $G/nonogram.c $G/cryptogram.c"
build d "$G/dict_words.c $G/wordgrid.c $G/wordladder.c $G/crisscross.c"
# suite e: tic-tac-toe, connect 4, and the per-level difficulty checks over the whole registry (wordsearch left out)
E=; for f in $G/*.c; do case $f in */wordsearch.c) ;; *) E="$E $f" ;; esac; done
build e "$E"
fail=0
for s in a b c d e; do
	arg=$mode
	[ $s = a ] && { [ "$mode" = full ] && arg=5 || arg=1; } # suite a takes a scale number
	[ $s = e ] && { [ "$mode" = full ] && arg=2 || arg=1; } # suite e takes a scale number too
	if "$out/$s" "$arg" > "$out/$s.log" 2>&1; then echo "suite $s: ok ($(tail -1 "$out/$s.log"))"
	else echo "suite $s: FAILED"; tail -20 "$out/$s.log"; fail=1; fi
done
exit $fail
