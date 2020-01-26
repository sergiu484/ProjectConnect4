#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define LISTEN_BACKLOG 5
#define MAX_ALLOWED_CONNECTED_CLIENTS 100
#define MAX_SECONDS_ALLOWED_PER_TURN 30
#define MAX_SECONDS_ALLOWED_FOR_REMATCH_ANSWER 30

int numberOfConnectedClients;

typedef struct ThreadData {
	int playerOneSd;
	int playerTwoSd;
} ThreadData;

typedef struct GameBoard {
	char board[6][7];
	char winningColor;
} GameBoard;

void initGameBoard(GameBoard *gameBoard) {
	for (int i = 0; i < 6; ++i) {
		for (int j = 0; j < 7; ++j) {
			gameBoard -> board[i][j] = '-';
		}
	}

	gameBoard -> winningColor = '-';
}

int generateRandomValidMove(GameBoard *gameBoard) {
	int move;

	do {
		move = rand() % 7;

		if (gameBoard -> board[5][move] == '-') {
			break;
		}
	} while (1);

	return move;
}

int isMoveValid(GameBoard *gameBoard, int move) {
	if (move < 0 || move > 6) {
		return 0;
	} else if (gameBoard -> board[5][move] != '-') {
		return 0;
	}

	return 1;
}

int possibleDirections = 8;
int linDirections[] = {1, 1, 0, -1, -1, -1, 0, 1};
int colDirections[] = {0, 1, 1, 1, 0, -1, -1, -1};

void updateGameBoard(GameBoard *gameBoard, int move, char c) {
	for (int i = 5; i >= 0; --i) {
		if (gameBoard -> board[i][move] != '-' || i == 0) {
			if (i == 0 && gameBoard -> board[i][move] == '-') {
				gameBoard -> board[i][move] = c;
				break;
			} else {
				gameBoard -> board[i + 1][move] = c;
				break;
			}
		}
	}

	for (int lin = 0; lin < 6; ++lin) {
		for (int col = 0; col < 7; ++col) {
			if (gameBoard -> board[lin][col] == c) {
				for (int direction = 0; direction < possibleDirections; ++direction) {
					int k;

					for (k = 1; k <= 3; ++k) {
						if (lin + linDirections[direction] * k < 0 || 
							lin + linDirections[direction] * k > 5 || 
							col + colDirections[direction] * k < 0 || 
							col + colDirections[direction] * k > 6 || 
							gameBoard -> board[lin + linDirections[direction] * k][col + colDirections[direction] * k] != c) 
						{
							break;
						}
					}

					if (k == 4) {
						gameBoard -> winningColor = c;
						return;
					}
				}
			}
		}
	}
}


void printBoardState(GameBoard *gameBoard) {
	pthread_t thId = pthread_self();
    printf("[%ld] Current game board state:\n", thId);

    for (int i = 5; i >= 0; --i) {
        for (int j = 0; j < 7; ++j) {
            if (gameBoard -> board[i][j] == 'R') {
                printf("R ");
            } else if (gameBoard -> board[i][j] == 'Y') {
                printf("Y ");
            } else {
                printf("- ");
            }
        }

        printf("\n");
    }
}

int startGame(int playerOneSd, int playerTwoSd, int *playerOneScore, int *playerTwoScore) {
	int startingPlayer = rand() % 2, firstPlayerSd, secondPlayerSd, isPlayerOneStarting, isPlayerTwoStarting, r, move, currentPlayerSd, waitingPlayerSd, timeoutSec;
	struct timeval timeout;
	GameBoard *gameBoard;
	pthread_t thId = pthread_self();

	printf("[%ld] Randomly selected which player starts the game ...\n", thId);
	if (startingPlayer == 0) {
		printf("[%ld] Player one has been chosen to start the game!\n", thId);
		firstPlayerSd = playerOneSd;
		secondPlayerSd = playerTwoSd;

		isPlayerOneStarting = 1;
		isPlayerTwoStarting = 0;
	} else {
		printf("[%ld] Player two has been chosen to start the game!\n", thId);
		firstPlayerSd = playerTwoSd;
		secondPlayerSd = playerOneSd;

		isPlayerOneStarting = 0;
		isPlayerTwoStarting = 1;
	}

	printf("[%ld] Announcing players the order in which they start the game ...\n", thId);
	if (write(playerOneSd, &isPlayerOneStarting, sizeof(isPlayerOneStarting)) == -1) {
		fprintf(stderr, "[%ld] Error announcing player one the order in which the game starts! [%d:%s]\n", thId, errno, strerror(errno));
		return -1;
	}

	if (write(playerTwoSd, &isPlayerTwoStarting, sizeof(isPlayerTwoStarting)) == -1) {
		fprintf(stderr, "[%ld] Error announcing player two the order in which the game starts! [%d:%s]\n", thId, errno, strerror(errno));
		return -1;
	}
	printf("[%ld] Successfully announced players the order in which they start the game!\n", thId);

	printf("[%ld] Anncouncing players the score ...\n", thId);
	if (write(playerOneSd, playerOneScore, sizeof(*playerOneScore)) == -1) {
		fprintf(stderr, "[%ld] Error announcing player one the player one score! [%d:%s]\n", thId, errno, strerror(errno));
		return -1;
	}

	if (write(playerOneSd, playerTwoScore, sizeof(*playerTwoScore)) == -1) {
		fprintf(stderr, "[%ld] Error announcing player one the player two score! [%d:%s]\n", thId, errno, strerror(errno));
		return -1;
	}

	if (write(playerTwoSd, playerOneScore, sizeof(*playerOneScore)) == -1) {
		fprintf(stderr, "[%ld] Error announcing player two the player one score! [%d:%s]\n", thId, errno, strerror(errno));
		return -1;
	}

	if (write(playerTwoSd, playerTwoScore, sizeof(*playerTwoScore)) == -1) {
		fprintf(stderr, "[%ld] Error announcing player two the player two score! [%d:%s]\n", thId, errno, strerror(errno));
		return -1;
	}
	printf("[%ld] Successfully announced players the game score!\n", thId);

	printf("[%ld] Initializing the game board for the game ...\n", thId);
	gameBoard = (GameBoard *) malloc(sizeof(GameBoard));
	initGameBoard(gameBoard);
	printf("[%ld] Game board successfully initialized!\n", thId);
	printBoardState(gameBoard);

	currentPlayerSd = firstPlayerSd;
	waitingPlayerSd = secondPlayerSd;

	do {
		timeoutSec = MAX_SECONDS_ALLOWED_PER_TURN;

		do {
			printf("[%ld] Setting wait time for current player move at %d seconds ...\n", thId, timeoutSec);
			timeout.tv_sec = timeoutSec;
			timeout.tv_usec = 0;

			if (setsockopt(currentPlayerSd, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout)) == -1) {
				fprintf(stderr, "[%ld] Failed to set SO_RCVTIMEO on player socket! [%d:%s]\n", thId, errno, strerror(errno));
				return -1;
			}

			time_t readingStartTime = time(NULL);
			
			printf("[%ld] Waiting for move from current player with a timeout of %d seconds ...\n", thId, timeoutSec);
			r = read(currentPlayerSd, &move, sizeof(move));

			if (r == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
				fprintf(stderr, "[%ld] Error reading move from current player! [%d:%s]\n", thId, errno, strerror(errno));
				return -1;
			} else if (r == 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
				printf("[%ld] Current player closed connection to server!\n", thId);
				return -1;
			} else if (errno == EWOULDBLOCK || errno == EAGAIN) {
				printf("[%ld] Current player failed to send move in time, generating a random move instead ...\n", thId);
				move = generateRandomValidMove(gameBoard);
				char c = (currentPlayerSd == firstPlayerSd) ? 'R' : 'Y';
				printf("[%ld] Generated random move add a '%c' disk on column %d!\n", thId, c, move);

				printf("[%ld] Updating game board ...\n", thId);
				updateGameBoard(gameBoard, move, c);
				printf("[%ld] Game board successfully updated!\n", thId);
				printBoardState(gameBoard);
			
				printf("[%ld] Announcing players the new game board state ...\n", thId);
				if (write(playerOneSd, gameBoard, sizeof(GameBoard)) == -1) {
					fprintf(stderr, "[%ld] Error writing player one the game board state with random generated move! [%d:%s]\n", thId, errno, strerror(errno));
					return -1;
				}

				if (write(playerTwoSd, gameBoard, sizeof(GameBoard)) == -1) {
					fprintf(stderr, "[%ld] Error writing player two the game board state with random generated move! [%d:%s]\n", thId, errno, strerror(errno));
					return -1;
				}
				printf("[%ld] Successfuly announced players the new game board state!\n", thId);

				break;
			} else if (!isMoveValid(gameBoard, move)) {
				char c = (currentPlayerSd == firstPlayerSd) ? 'R' : 'Y';
				printf("[%ld] Current player submited invalid move add '%c' disk on column %d!\n", thId, c, move);

				printf("[%ld] Updating the time remaining for the current player to make a move ...\n", thId);
				time_t timeSinceReadingStartTime = time(NULL);
				timeoutSec -= (timeSinceReadingStartTime - readingStartTime);
				printf("[%ld] New time remaning for submiting a move is %d seconds!\n", thId, timeoutSec);

				printf("[%ld] Announcing current player that the move he submitted is invalid ...\n", thId);
				int isMoveAccepted = 0;
				if (write(currentPlayerSd, &isMoveAccepted, sizeof(isMoveAccepted)) == -1) {
					fprintf(stderr, "[%ld] Error announcing the current player invalid move status! [%d:%s]\n", thId, errno, strerror(errno));
					return -1;
				}
				printf("[%ld] Successfully announced current player that the move he submitted is invalid!\n", thId);

				printf("[%ld] Annoucning current player the time he has left to submit a move ...\n", thId);
				if (write(currentPlayerSd, &timeoutSec, sizeof(timeoutSec)) == -1) {
					fprintf(stderr, "[%ld] Error writing the current player number of seconds left for move! [%d:%s]\n", thId, errno, strerror(errno));
					return -1;
				}
				printf("[%ld] Successfully announced player the time he has left to make a move!\n", thId);

				continue;
			} else {
				char c = (currentPlayerSd == firstPlayerSd) ? 'R' : 'Y';
				printf("[%ld] Recieved valid move from current player! Move is add '%c' disk on column %d!\n", thId, c, move);

				printf("[%ld] Announcing current player that the move he submitted in valid ...\n", thId);
				int isMoveAccepted = 1;
				if (write(currentPlayerSd, &isMoveAccepted, sizeof(isMoveAccepted)) == -1) {
					fprintf(stderr, "[%ld] Error announcing the current player valid move status! [%d:%s]\n", thId, errno, strerror(errno));
					return -1;
				}
				printf("[%ld] Successfully announced current player that the move he submitted is valid!\n", thId);
	
				printf("[%ld] Updating game board by adding a '%c' disk on column %d!\n", thId, c, move);
				updateGameBoard(gameBoard, move, c);
				printf("[%ld] Updated game board successfully!\n", thId);
				printBoardState(gameBoard);	
				
				printf("[%ld] Announcing players the new game board state ...\n", thId);	
				if (write(playerOneSd, gameBoard, sizeof(GameBoard)) == -1) {
					fprintf(stderr, "[%ld] Error writing player one the game board state with random generated move! [%d:%s]\n", thId, errno, strerror(errno));
					return -1;
				}

				if (write(playerTwoSd, gameBoard, sizeof(GameBoard)) == -1) {
					fprintf(stderr, "[%ld] Error writing player two the game board state with random generated move! [%d:%s]\n", thId, errno, strerror(errno));
					return -1;
				}
				printf("[%ld] Successfuly announced players the new game board state!\n", thId);

				break;
			}
		} while(1);

		if (currentPlayerSd == firstPlayerSd) {
			currentPlayerSd = secondPlayerSd;
			waitingPlayerSd = firstPlayerSd;
		} else {
			currentPlayerSd = firstPlayerSd;
			waitingPlayerSd = secondPlayerSd;
		}

		if (gameBoard -> winningColor != '-') {
			printf("[%ld] Player with color '%c' won the game, exiting game ...\n", thId, gameBoard -> winningColor);
			if (gameBoard -> winningColor == 'R') {
				if (firstPlayerSd == playerOneSd) {
					*playerOneScore += 1;
				} else {
					*playerTwoScore += 1;
				}
			} else {
				if (secondPlayerSd == playerOneSd) {
					*playerOneScore += 1;
				} else {
					*playerTwoScore += 1;
				}
			}

			break;
		}

		errno = 0;
	} while(1);

	return 0;
}

static void *treat(void *arg) {
	ThreadData td;
	pthread_t thId = pthread_self();
	int playerOneWantsRematch, playerTwoWantsRematch, r;
	struct timeval timeout;
	fd_set inputSet;
	int playerOneScore = 0, playerTwoScore = 0;

	td = *((ThreadData*) arg);

	pthread_detach(thId);

	printf("[%ld] Starting a new game series ...\n", thId);
	do {
		printf("[%ld] Starting a new game ...\n", thId);

		if (startGame(td.playerOneSd, td.playerTwoSd, &playerOneScore, &playerTwoScore) == -1) {
			break;
		}

		printf("[%ld] Game ended, checking if players want to rematch ...\n", thId);

		playerOneWantsRematch = -1;
		playerTwoWantsRematch = -1;

		timeout.tv_sec = MAX_SECONDS_ALLOWED_FOR_REMATCH_ANSWER;
		timeout.tv_usec = 0;

		FD_ZERO(&inputSet);
		FD_SET(td.playerOneSd, &inputSet);
		FD_SET(td.playerTwoSd, &inputSet);

		printf("[%ld] Waiting for one of the players to send rematch answer ...\n", thId);
		int ndfs = td.playerOneSd > td.playerTwoSd ? td.playerOneSd + 1 : td.playerTwoSd + 1;
		time_t readingStartTime = time(NULL);
		r = select(ndfs, &inputSet, NULL, NULL, &timeout);

		if (r == -1) {
			fprintf(stderr, "[%ld] Error at select call waiting for players to answer if rematch! [%d:%s]\n", thId, errno, strerror(errno));
			break;
		} else if (r) {
			if (FD_ISSET(td.playerOneSd, &inputSet)) {
				if (read(td.playerOneSd, &playerOneWantsRematch, sizeof(playerOneWantsRematch)) == -1) {
					fprintf(stderr, "[%ld] Error reading if player one wants to rematch! [%d:%s]\n", thId, errno, strerror(errno));
					break;
				}

				printf("[%ld] Got answer from player one '%d'!\n", thId, playerOneWantsRematch);
			}

			if (FD_ISSET(td.playerTwoSd, &inputSet)) {
				if (read(td.playerTwoSd, &playerTwoWantsRematch, sizeof(playerTwoWantsRematch)) == -1) {
					fprintf(stderr, "[%ld] Error reading if player one wants to rematch! [%d:%s]\n", thId, errno, strerror(errno));
					break;
				}

				printf("[%ld] Got answer from player two '%d'!\n", thId, playerTwoWantsRematch);
			}
		} else {
			printf("[%ld] Players did not respond in time!\n", thId);
			break;
		}

		if (playerOneWantsRematch == 0 || playerTwoWantsRematch == 0) {
			printf("[%ld] One of the players answers negative on rematch request!\n", thId);
			break;
		}

		if (playerOneWantsRematch == 1 && playerTwoWantsRematch == 1) {
			printf("[%ld] Both players agreed on rematch!\n", thId);
			continue;
		}

		time_t timeSinceReadingStartTime = time(NULL) - readingStartTime;
		timeout.tv_sec = MAX_SECONDS_ALLOWED_FOR_REMATCH_ANSWER - timeSinceReadingStartTime;
		timeout.tv_usec = 0;

		FD_ZERO(&inputSet);
		if (playerOneWantsRematch) {
			printf("[%ld] Waiting for answer from player two if rematch is wanted ...\n", thId);
			FD_SET(td.playerTwoSd, &inputSet);
		} else {
			printf("[%ld] Waiting for answer from player one if rematch is wanted ...\n", thId);
			FD_SET(td.playerOneSd, &inputSet);
		}

		r = select(ndfs, &inputSet, NULL, NULL, &timeout);

		if (r == -1) {
			fprintf(stderr, "[%ld] Error at select call waiting for last player to answer if rematch! [%d:%s]\n", thId, errno, strerror(errno));
			break;
		} else if (r) {
			if (FD_ISSET(td.playerOneSd, &inputSet)) {
				if (read(td.playerOneSd, &playerOneWantsRematch, sizeof(playerOneWantsRematch)) == -1) {
					fprintf(stderr, "[%ld] Error reading if player one wants to rematch! [%d:%s]\n", thId, errno, strerror(errno));
					break;
				}

				printf("[%ld] Got answer from player one '%d'!\n", thId, playerOneWantsRematch);
			}

			if (FD_ISSET(td.playerTwoSd, &inputSet)) {
				if (read(td.playerTwoSd, &playerTwoWantsRematch, sizeof(playerTwoWantsRematch)) == -1) {
					fprintf(stderr, "[%ld] Error reading if player one wants to rematch! [%d:%s]\n", thId, errno, strerror(errno));
					break;
				}

				printf("[%ld] Got answer from player two '%d'!\n", thId, playerTwoWantsRematch);
			}
		} else {
			printf("[%ld] Last player did not answer in time\n", thId);
			break;
		}

		if (playerOneWantsRematch == 1 && playerTwoWantsRematch == 1) {
			printf("[%ld] Players chose to rematch!\n", thId);
			continue;
		} else {
			printf("[%ld] Players chose not to rematch!\n", thId);
			break;
		}
	} while(1);

		printf("[%ld] Game series ended, closing connections with the players ...\n", thId); 
		close(td.playerOneSd);
		close(td.playerTwoSd);
		numberOfConnectedClients -= 2;

		printf("[%ld] Closed connections successfully, thread exiting!\n", thId);
		return NULL;
	}

int main(int argc, char *argv[]) {
	int port, serverSd, playerOneSd, playerTwoSd, threadCreationErrno;
	struct sockaddr_in server;
	ThreadData *td;
	pthread_t thId;

	if (argc != 2) {
		fprintf(stderr, "[SERVER] Usage: $ %s {{ PORT }}\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	char *strtolPtrEnd;
	port = strtol(argv[1], &strtolPtrEnd, 10);
	if (errno) {
		fprintf(stderr, "[SERVER] Port '%s' is invalid! [%d:%s]\n", argv[1], errno, strerror(errno));
		exit(EXIT_FAILURE);
	} else if (*strtolPtrEnd != '\0') {
		fprintf(stderr, "[SERVER] Port '%s' is inavlid!\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	if ((serverSd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "[SERVER] Socket creation failed! [%d:%s]\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	int optval = 1;
	if (setsockopt(serverSd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
		fprintf(stderr, "[SERVER] Failed to set SO_REUSEADDRD on socket! [%d:%s]\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	bzero(&server, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(port);

	if (bind(serverSd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1) {
		fprintf(stderr, "[SERVER] Binding socket failed!\n [%d:%s]\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (listen(serverSd, LISTEN_BACKLOG) == -1) {
		fprintf(stderr, "[SERVER] Listen call failed! [%d:%s]\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	numberOfConnectedClients = 0;
	srand(time(NULL));

	printf("[SERVER] Started successfully at port %d!\n", port);
	while(1) {
		printf("[SERVER] Waiting for a client to connect as player one ...\n");
		playerOneSd = accept(serverSd, NULL, NULL);
		printf("[SERVER] A client connected as player one!\n");

		if (playerOneSd == -1) {
			fprintf(stderr, "[SERVER] Error accepting the client which connected as player one! [%d:%s]\n", errno, strerror(errno));
			continue;
		}

		numberOfConnectedClients += 1;
		if (numberOfConnectedClients >= MAX_ALLOWED_CONNECTED_CLIENTS) {
			printf("[SERVER] Maximum number of clients already connected! Closing connection with the client previously connected as player one!\n");
			close(playerOneSd);
			numberOfConnectedClients -= 1;

			continue;
		}

		printf("[SERVER] Waiting for a client to connect as player two ...\n");
		while(1) {
			playerTwoSd = accept(serverSd, NULL, NULL);
			printf("[SERVER] A client connected as player two!\n");

			if (playerTwoSd == -1) {
				fprintf(stderr, "[SERVER] Error accepting the client which connected as player two! [%d:%s]\n", errno, strerror(errno));
				continue;
			}

			break;
		}

		numberOfConnectedClients++;

		printf("[SERVER] Creating a new thread to handle the clients just connected ...\n");
		td = (ThreadData*) malloc(sizeof(ThreadData));
		td -> playerOneSd = playerOneSd;
		td -> playerTwoSd = playerTwoSd;

		threadCreationErrno = pthread_create(&thId, NULL, &treat, td);
		if (threadCreationErrno) {
			printf("[SERVER] Failed to create new thread for the clients just connected! [%d:%s]\n", threadCreationErrno, strerror(threadCreationErrno));

			close(playerOneSd);
			close(playerTwoSd);

			numberOfConnectedClients -= 2;
			continue;
		}

		printf("[SERVER] Thread creation ended successfully! Thread ID: %ld\n", thId);
	}
}
