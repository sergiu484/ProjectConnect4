#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>

#define RED "\x1B[31m"
#define YEL "\x1B[33m"
#define RESET "\x1B[0m"

#define MAX_SECONDS_ALLOWED_PER_TURN 30

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

void printBoardState(GameBoard *gameBoard) {
	printf("\n");

	for (int i = 0; i < 7; ++i) {
		printf("%d ", i);
	} printf("\n");

	for (int i = 5; i >= 0; --i) {
		for (int j = 0; j < 7; ++j) {
			if (gameBoard -> board[i][j] == 'R') {
				printf(RED "R " RESET);
			} else if (gameBoard -> board[i][j] == 'Y') {
				printf(YEL "Y " RESET);
			} else {
				printf("- ");
			}
		}

		printf("\n");
	}

	for (int i = 0; i < 7; ++i) {
		printf("%d ", i);
	} printf("\n");

	printf("\n");
}

int main(int argc, char *argv[]) {
	int port, serverSd, r, timeRemainingForMove, moveStatus, move, choseRematch;
	char playerColor;
	struct sockaddr_in server;
	GameBoard *gameBoard;
	struct timeval timeout;
	fd_set inputSet;

	if (argc != 3) {
		fprintf(stderr, "Usage: $ %s {{ SERVER_IP_ADRESS }} {{ SERVER_PORT }}\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	char *strtolPtrEnd;
    port = strtol(argv[2], &strtolPtrEnd, 10);
    if (errno) {
        fprintf(stderr, "Port '%s' is invalid! [%d:%s]\n", argv[1], errno, strerror(errno));
        exit(EXIT_FAILURE);
    } else if (*strtolPtrEnd != '\0') {
        fprintf(stderr, "Port '%s' is inavlid!\n", argv[1]);
        exit(EXIT_FAILURE);
    }

	if (inet_pton(AF_INET, argv[1], &server.sin_addr) == 0) {
		fprintf(stderr, "Server IP adress '%s' is invalid!\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	if ((serverSd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "Error while creating the socket! [%d:%s]\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	bzero(&server, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(argv[1]);
	server.sin_port = htons (port);

	if (connect(serverSd, (struct sockaddr *) &server,sizeof (struct sockaddr)) == -1) {
		fprintf(stderr, "Error connecting to server! [%d:%s]\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Welcome to Connect4!\n");
	printf("Looking for an opponent ...\n");

	int atLeastOneGamePlayed = 0;
	do {
		choseRematch = 0;
		int isStartingPlayer;

		if ((r = read(serverSd, &isStartingPlayer, sizeof(isStartingPlayer))) == -1) {
			fprintf(stderr, "Error reading game start order message from server! [%d:%s]\n", errno, strerror(errno));
			close(serverSd);
			exit(EXIT_FAILURE);
		} else if (r == 0) {
			if (atLeastOneGamePlayed) {
				printf("Your opponent chose not to rematch!\n");
				close(serverSd);
				return 0;
			}

			printf("We're sorry! Our server is full at the moment, please try again later!\n");
			close(serverSd);
			return 0;
		}

		if (atLeastOneGamePlayed) {
			printf("Your opponent chose to rematch aswell!\n");
		} else {
			printf("Game found!\n");
		}

		int playerOneScore, playerTwoScore;

		if (read(serverSd, &playerOneScore, sizeof(playerOneScore)) == -1) {
			fprintf(stderr, "Error reading player one score from server! [%d:%s]\n", errno, strerror(errno));
			close(serverSd);
			exit(EXIT_FAILURE);
		}

		if (read(serverSd, &playerTwoScore, sizeof(playerTwoScore)) == -1) {
			fprintf(stderr, "Error reading player two score from server! [%d:%s]\n", errno, strerror(errno));
			close(serverSd);
			exit(EXIT_FAILURE);
		}

		printf("Player One %d - %d Player Two\n", playerOneScore, playerTwoScore);

		gameBoard = (GameBoard *) malloc(sizeof(GameBoard));
		initGameBoard(gameBoard);

		printf("Initial board state:\n");
		printBoardState(gameBoard);

		printf("A move is a number from 0 to 6 representing the column in which you want to drop a disk!\n");

		if (isStartingPlayer) {
			printf("You are the player starting the game!\n");
			playerColor = 'R';
		} else {
			printf("Your opponent will start the game, waiting for his move ...\n");
			playerColor = 'Y';

			if ((r = read(serverSd, gameBoard, sizeof(GameBoard))) == -1) {
				fprintf(stderr, "Error reading the game board at opponent move! [%d:%s]\n", errno, strerror(errno));
				close(serverSd);
				exit(EXIT_FAILURE);
			} else if (r == 0) {
				printf("Your opponent disconnected from the game!\n");
				close(serverSd);
				return 0;
			}

			printf("Your opponent moved! New board state:\n");
			printBoardState(gameBoard);
		}

		do {
			timeRemainingForMove = MAX_SECONDS_ALLOWED_PER_TURN;
			moveStatus = 0;

			do {
				FD_ZERO(&inputSet);
				FD_SET(0, &inputSet);

				timeout.tv_sec = timeRemainingForMove;
				timeout.tv_usec = 0;

				printf("Enter a move (%d seconds remaining): ", timeRemainingForMove);
				fflush(stdout);

				r = select(1, &inputSet, NULL, NULL, &timeout);

				if (r == -1) {
					fprintf(stderr, "Error at select call! [%d:%s]\n", errno, strerror(errno));
					close(serverSd);
					exit(EXIT_FAILURE);
				} else if (r) {
					scanf("%d", &move);
				} else {
					moveStatus = -1;
					break;
				}

				printf("You chose to add a '%c' disk on column %d!\n", playerColor, move);

				if (write(serverSd, &move, sizeof(move)) == -1) {
					fprintf(stderr, "Error writing your move to the server! [%d:%s]\n", errno, strerror(errno));
					close(serverSd);
					exit(EXIT_FAILURE);
				}

				int isMoveAccepted;
				if ((r = read(serverSd, &isMoveAccepted, sizeof(isMoveAccepted))) == -1) {
					fprintf(stderr, "Error reading the move validity from the server! [%d:%s]\n", errno, strerror(errno));
					close(serverSd);
					exit(EXIT_FAILURE);
				} else if (r == 0) {
					printf("Your opponent disconnected from the game!\n");
					return 0;
				}

				if (isMoveAccepted) {
					break;
				} else {
					printf("Invalid move, please enter a valid move!\n");
					if ((r = read(serverSd, &timeRemainingForMove, sizeof(timeRemainingForMove))) == -1) {
						fprintf(stderr, "Error reading the time remaining for move from server! [%d:%s]\n", errno, strerror(errno));
						close(serverSd);
						exit(EXIT_FAILURE);
					}

					continue;
				}
			} while (1);

			if (moveStatus == 0) {
				printf("You entered a valid move! Reading new board state from server ...\n");
			} else {
				printf("\nYour time run out, the server generated a random move for you! Reading new board state from server ...\n");
			}

			if ((r = read(serverSd, gameBoard, sizeof(GameBoard))) == -1) {
				fprintf(stderr, "Error reading the game board state after local player move! [%d:%s]\n", errno, strerror(errno));
				close(serverSd);
				exit(EXIT_FAILURE);
			} else if (r == 0) {
				printf("Your opponent disconnected from the game!\n");
				close(serverSd);
				return 0;
			}

			printf("Board state successfully read! New board state:\n");
			printBoardState(gameBoard);

			if (gameBoard -> winningColor != '-') {
				if ((isStartingPlayer && gameBoard -> winningColor == 'R') || (!isStartingPlayer && gameBoard -> winningColor == 'Y')) {
					printf("Congratulations, you won the game!\n");
				} else {
					printf("You lost!\n");
				}

				FD_ZERO(&inputSet);
				FD_SET(0, &inputSet);

				timeout.tv_sec = 30;
				timeout.tv_usec = 0;

				printf("Would you like a rematch? (30 seconds remaining) [Y/n]: ");
				fflush(stdout);

				r = select(1, &inputSet, NULL, NULL, &timeout);

				char selection[10];
				bzero(selection, 10);
				if (r == -1) {
					fprintf(stderr, "Error at select call! [%d:%s]\n", errno, strerror(errno));
					close(serverSd);
					exit(EXIT_FAILURE);
				} else if (r) {
					scanf("%s", selection);
					printf("You chose answer '%s'!\n", selection);
					if (selection[strlen(selection) - 1] == '\n') selection[strlen(selection) - 1] = '\0';
				} else {
					printf("You did not answer in time, exiting game ...\n");
					break;
				}

				int selectionInt;
				if (strcmp(selection, "Y") == 0) {
					selectionInt = 1;
				} else {
					selectionInt = 0;
				}

				if (write(serverSd, &selectionInt, sizeof(selectionInt)) == -1) {
					fprintf(stderr, "Error writing to server if rematch is wanted! [%d:%s]\n", errno, strerror(errno));
					close(serverSd);
					exit(EXIT_FAILURE);
				}

				if (selectionInt == 0) {
					printf("You chose not to rematch, exiting game ...\n");
					break;
				} else {
					printf("You chose to rematch, waiting for your opponent's choice ...\n");
					choseRematch = 1;
					break;
				}
			}

			printf("Waiting for your opponent's move ...\n");

			if ((r = read(serverSd, gameBoard, sizeof(GameBoard))) == -1) {
				fprintf(stderr, "Error reading the game board at opponent move! [%d:%s]\n", errno, strerror(errno));
				close(serverSd);
				exit(EXIT_FAILURE);
			} else if (r == 0) {
				printf("Your opponent disconnected from the game!\n");
				close(serverSd);
				return 0;
			}

			printf("Your opponent moved! New board state:\n");
			printBoardState(gameBoard);

			if (gameBoard -> winningColor != '-') {
				atLeastOneGamePlayed = 1;
				if ((isStartingPlayer && gameBoard -> winningColor == 'R') || (!isStartingPlayer && gameBoard -> winningColor == 'Y')) {
					printf("Congratulations, you won the game!\n");
				} else {
					printf("You lose!\n");
				}

				FD_ZERO(&inputSet);
				FD_SET(0, &inputSet);

				timeout.tv_sec = 30;
				timeout.tv_usec = 0;

				printf("Would you like a rematch? (30 seconds remaining) [Y/n]: ");
				fflush(stdout);

				r = select(1, &inputSet, NULL, NULL, &timeout);

				char selection[10];
				bzero(selection, 10);
				if (r == -1) {
					fprintf(stderr, "Error at select call! [%d:%s]\n", errno, strerror(errno));
					close(serverSd);
					exit(EXIT_FAILURE);
				} else if (r) {
					scanf("%s", selection);
					printf("You chose answer '%s'!\n", selection);
					if (selection[strlen(selection) - 1] == '\n') selection[strlen(selection) - 1] = '\0';
				} else {
					printf("\nYou did not answer in time, exiting game ...\n");
					break;
				}

				int selectionInt;
				if (strcmp(selection, "Y") == 0) {
					selectionInt = 1;
				} else {
					selectionInt = 0;
				}

				if (write(serverSd, &selectionInt, sizeof(selectionInt)) == -1) {
					fprintf(stderr, "Error writing to server if rematch is wanted! [%d:%s]\n", errno, strerror(errno));
					close(serverSd);
					exit(EXIT_FAILURE);
				}

				if (selectionInt == 0) {
					printf("You chose not to rematch, exiting game ...\n");
					break;
				} else {
					printf("You chose to rematch, waiting for your opponent's choice ...\n");
					choseRematch = 1;
					break;
				}
			}
		} while(1);
	} while(choseRematch);

	close(serverSd);
	return 0;
}
