/* The local games, in menu order. Each is a weak symbol: a game that isn't built (yet) is simply skipped, so game
 * files can be added independently (one file per game, defining `const GameDef GAME_<id>`). */
#include "../games.h"

#define G(id) extern const GameDef GAME_##id __attribute__((weak));
G(tictactoe) G(connect4) G(dots) G(gomoku) G(reversi) G(battleship) G(nim) G(hangman) G(minesweeper) G(sudoku)
G(memory) G(wordsearch) G(codebreaker) G(lightsout) G(sliding)
G(maze) G(nonogram) G(cryptogram) G(wordgrid) G(wordladder) G(crisscross)
#undef G

const GameDef *const GAMES[] = {
	&GAME_tictactoe, &GAME_connect4, &GAME_dots, &GAME_gomoku, &GAME_reversi, &GAME_battleship, &GAME_nim,
	&GAME_hangman, &GAME_minesweeper, &GAME_sudoku, &GAME_memory, &GAME_wordsearch, &GAME_codebreaker,
	&GAME_lightsout, &GAME_sliding, &GAME_maze, &GAME_nonogram, &GAME_cryptogram, &GAME_wordgrid, &GAME_wordladder,
	&GAME_crisscross,
};
const int NGAMES_ALL = (int)(sizeof GAMES / sizeof GAMES[0]);
