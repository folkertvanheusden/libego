//
// Copyright 2023 and onwards, Folkert van Heusden
//

#include "board.hpp"
#include "perft.hpp"

namespace Perft {
	uint64_t perft(const Board & board, const Player & p, const int depth) {
		uint64_t count = 0;

		int dim = board.Size();

		Player new_player = p.Other();

		int new_depth = depth - 1;

		for(int y=0; y<dim; y++) {
			for(int x=0; x<dim; x++)  {
				Vertex v = Vertex::OfCoords(x, y);
				Move m(p, v);

				if (board.IsLegal(m) == false)
					continue;

				Board copy;
				copy.Load(board);

				copy.PlayLegal(m);

				if (new_depth)
					count += perft(copy, new_player, new_depth);
				else
					count++;
			}
		}

		return count;
	}

	string Run (uint depth) {
		Board board;

		int dim = board.Size();

		for (int i=1; i<=depth; i++) {
			printf("%d: %lu\n", i, perft(board, Player::Black(), i));
		}
	}
}
