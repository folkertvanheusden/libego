//
// Copyright 2023 and onwards, Folkert van Heusden
//

#include "board.hpp"
#include "perft.hpp"

namespace Perft {
	uint64_t perft(const Board & board, const Player & p, const int depth, const int pass) {
		if (depth == 0)
			return 1;

		uint64_t count = 0;

		int dim = board.Size();

		Player new_player = p.Other();

		int new_depth = depth - 1;

		for(int y=0; y<dim; y++) {
			for(int x=0; x<dim; x++)  {
				Move m(p, Vertex::OfCoords(x, y));

				if (board.IsLegal(m) == false)
					continue;

				Board copy;
				copy.Load(board);

				copy.PlayLegal(m);

				if (new_depth)
					count += perft(copy, new_player, new_depth, 0);
				else
					count++;
			}
		}

		if (pass == 0)
			count += perft(board, new_player, new_depth, pass + 1);

		return count;
	}

	string Run (uint depth) {
		Board board;

		int dim = board.Size();

		for (int i=1; i<=depth; i++) {
			printf("%d: %lu\n", i, perft(board, Player::Black(), i, 0));
		}

		return "";
	}
}
