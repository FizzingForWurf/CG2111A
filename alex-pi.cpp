#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdint.h>
#include <cctype>
#include "packet.h"
#include "serial.h"
#include "serialize.h"
#include "constants.h"
#include <string.h>

#include <unistd.h>
#include <termios.h>

#define PORT_NAME			"/dev/ttyACM0"
#define BAUD_RATE			B9600

int exitFlag=0;
int manFlag = 0;
TTokenType tokenStatuses[3] = {TOKEN_GOOD};

sem_t _xmitSema;

char getch() {
	char buf = 0;
	struct termios old = {0};
	if (tcgetattr(0, &old) < 0)
			perror("tcsetattr()");
	old.c_lflag &= ~ICANON;
	old.c_lflag &= ~ECHO;
	old.c_cc[VMIN] = 1;
	old.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSANOW, &old) < 0)
			perror("tcsetattr ICANON");
	if (read(0, &buf, 1) < 0)
			perror ("read()");
	old.c_lflag |= ICANON;
	old.c_lflag |= ECHO;
	if (tcsetattr(0, TCSADRAIN, &old) < 0)
			perror ("tcsetattr ~ICANON");
	return (buf);
}

void handleError(TResult error) {
	switch(error) {
		case PACKET_BAD:
			printf("ERROR: Bad Magic Number\n");
			break;

		case PACKET_CHECKSUM_BAD:
			printf("ERROR: Bad checksum\n");
			break;

		default:
			printf("ERROR: UNKNOWN ERROR\n");
	}
}

void handleStatus(TPacket *packet) {
	printf("\n ------- ALEX STATUS REPORT ------- \n\n");
	printf("Left Forward Ticks:\t\t%d\n", packet->params[0]);
	printf("Right Forward Ticks:\t\t%d\n", packet->params[1]);
	printf("Left Reverse Ticks:\t\t%d\n", packet->params[2]);
	printf("Right Reverse Ticks:\t\t%d\n", packet->params[3]);
	printf("Left Forward Ticks Turns:\t%d\n", packet->params[4]);
	printf("Right Forward Ticks Turns:\t%d\n", packet->params[5]);
	printf("Left Reverse Ticks Turns:\t%d\n", packet->params[6]);
	printf("Right Reverse Ticks Turns:\t%d\n", packet->params[7]);
	printf("Forward Distance:\t\t%d\n", packet->params[8]);
	printf("Reverse Distance:\t\t%d\n", packet->params[9]);
	printf("\n---------------------------------------\n\n");
}

void handleResponse(TPacket *packet) {
	// The response code is stored in command
	switch(packet->command) {
		case RESP_OK:
			printf("Command OK\n");
		break;

		case RESP_STATUS:
			handleStatus(packet);
		break;

		default:
			printf("Arduino is confused\n");
	}
}

void handleErrorResponse(TPacket *packet) {
	// The error code is returned in command
	switch(packet->command) {
		case RESP_BAD_PACKET:
			printf("Arduino received bad magic number\n");
		break;

		case RESP_BAD_CHECKSUM:
			printf("Arduino received bad checksum\n");
		break;

		case RESP_BAD_COMMAND:
			printf("Arduino received bad command\n");
		break;

		case RESP_BAD_RESPONSE:
			printf("Arduino received unexpected response\n");
		break;
		case RESP_BAD_TOKEN:
		printf("Arduino received bad token in command parameters\n");
		break;
		default:
			printf("Arduino reports a weird error\n");
	}
}

void handleMessage(TPacket *packet) {
	printf("Message from Alex: %s\n", packet->data);
}

void handlePacket(TPacket *packet) {
	switch(packet->packetType) {
		case PACKET_TYPE_COMMAND:
			// Only we send command packets, so ignore
			break;

		case PACKET_TYPE_RESPONSE:
			handleResponse(packet);
			break;

		case PACKET_TYPE_ERROR:
			handleErrorResponse(packet);
			break;

		case PACKET_TYPE_MESSAGE:
			handleMessage(packet);
			break;
	}
}

void sendPacket(TPacket *packet) {
	char buffer[PACKET_SIZE];
	int len = serialize(buffer, packet, sizeof(TPacket));

	serialWrite(buffer, len);
}

void *receiveThread(void *p) {
	char buffer[PACKET_SIZE];
	int len;
	TPacket packet;
	TResult result;
	int counter=0;

	while(1) {
		len = serialRead(buffer);
		counter+=len;
		if(len > 0) {
			result = deserialize(buffer, len, &packet);

			if(result == PACKET_OK) {
				counter=0;
				handlePacket(&packet);
				
			} else if(result != PACKET_INCOMPLETE) {
				printf("PACKET ERROR\n");
				handleError(result);
			}
		}
	}
}

void flushInput() {
	char c;

	while((c = getchar()) != '\n' && c != EOF);
}

void getParams(TPacket *commandPacket) {
	printf("Enter distance/angle in cm/degrees (e.g. 50) and power in %% (e.g. 75) separated by space.\n");
	printf("E.g. 50 75 means go at 50 cm at 75%% power for forward/backward, or 50 degrees left or right turn at 75%%  power\n");
	scanf("%d %d", &commandPacket->params[0], &commandPacket->params[1]);
	flushInput();
}

TTokenType checkSpeedToken(char *userStr)
{
	char *junkStr;
	if (strtol(userStr, &junkStr,10) < 0 || strtol(userStr, &junkStr,10) > 100)
	{
		return SPEED_TOKEN_BAD;
	}
	else {
		return TOKEN_GOOD;
	}
}

TTokenType* checkTokens(char *userStr)
{
	char *junkStr;
	long checkSpeed = strtol(userStr, &junkStr, 10);
	long checkDist = strtol(userStr, &junkStr, 10);
	char checkDir = *junkStr;
	if ( checkSpeed < 0 || checkSpeed > 100 )
	{
       		tokenStatuses[0] = SPEED_TOKEN_BAD;
	}
	if (checkDist < DIST_MIN || checkDist > DIST_MAX)
	{
		tokenStatuses[1] = DIST_TOKEN_BAD;
	}
	if (checkDir != 'w' || checkDir != 'a' || checkDir != 's' || checkDir != 'd')
	{
		tokenStatuses[2] = DIR_TOKEN_BAD;
	}	
	return tokenStatuses;
}

void sendCommand(char* command) {
	TPacket commandPacket;
	commandPacket.packetType = PACKET_TYPE_COMMAND;
	
	if (strlen(command) == 1)
	{
		switch(*command) {
			case FORWARD: {
				// getParams(&commandPacket);
				// commandPacket.params[0] = 50; // Speed
				// commandPacket.params[1] = 10; // Distance
				printf("FORWARD\n");
				commandPacket.command = COMMAND_FORWARD;
				sendPacket(&commandPacket);
				break;
		         }
			case REVERSE: {
				// getParams(&commandPacket);
				printf("REVERSE\n");
				commandPacket.command = COMMAND_REVERSE;
				sendPacket(&commandPacket);
				break;
		         }
			case LEFT: {
				// getParams(&commandPacket);
				printf("LEFT\n");
				commandPacket.command = COMMAND_TURN_LEFT;
				sendPacket(&commandPacket);
				break;
				   }
			case RIGHT: {
				// getParams(&commandPacket);
				printf("RIGHT\n");
				commandPacket.command = COMMAND_TURN_RIGHT;
				sendPacket(&commandPacket);
				break;
				    }

			case STOP: {
				printf("STOP\n");
				commandPacket.command = COMMAND_STOP;
				sendPacket(&commandPacket);
				break;
				   }

			case CLEAR: {
				printf("CLEAR\n");
				commandPacket.command = COMMAND_CLEAR_STATS;
				// commandPacket.params[0] = 0;
				sendPacket(&commandPacket);
				break;
				    }

			case STATS: {
				printf("STATS\n");
				commandPacket.command = COMMAND_GET_STATS;
				sendPacket(&commandPacket);
				break; 
				    }
			case MANUAL: {
				manFlag = 1 - manFlag;
				printf("\nCurrent mode is %s: \n", manFlag ? "MANUAL" : "AUTO");
				break;
				     }
		//the speed config is designed for auto, use manual for more precision

			case SPEED_CONFIG: {
				printf("Enter Desired Preset Speed: ");
				char* userStr;
				userStr = fgets(userStr,MAX_STR_LEN,stdin);
				TTokenType tokenError = checkSpeedToken(userStr);
				if (tokenError)
				{
					break;
				}
				commandPacket.command = COMMAND_SPEED_CONFIG;
				for (int i = 0; i < MAX_STR_LEN; i++)
				{
					commandPacket.data[0] = userStr[0];
				}
				sendPacket(&commandPacket);
				break;
					   }
			case QUIT: {
				printf("QUIT\n");
				exitFlag=1;
				break;
				   }

			default: {
				printf("Bad command\n");
				 }
		}
	}
	else
	{
		int badTokens = 0;
		TTokenType* tokenError = checkTokens(command);
		for (int i = 0; i < 3; i++)
		{
			if (tokenError[i])
			{
				badTokens++;
				printf("Local token error %i\n", i+1);
			}	
		}
		if (!badTokens)
		{
			commandPacket.command = COMMAND_MANUAL;
			for (int i = 0; i < MAX_STR_LEN; i++)
			{
				commandPacket.data[i] = command[i];
			}
			sendPacket(&commandPacket);
		}
	}
}


int main()
{
	// Connect to the Arduino
	startSerial(PORT_NAME, BAUD_RATE, 8, 'N', 1, 5);

	// Sleep for two seconds
	printf("WAITING TWO SECONDS FOR ARDUINO TO REBOOT\n");
	sleep(2);
	printf("DONE\n");

	// Spawn receiver thread
	pthread_t recv;

	pthread_create(&recv, NULL, receiveThread, NULL);

	// Send a hello packet
	TPacket helloPacket;

	helloPacket.packetType = PACKET_TYPE_HELLO;
	sendPacket(&helloPacket);

	printf("\nToggle Mode = M, wasd for movement, f to stop ALEX, e = get stats, r = clear stats, q = quit\n");
	printf("\nRange of inputs in manual mode: Speed [0,100] , Distance [0,10], Direction [w,a,s,d]\n");
	printf("Format:PWM(percent) Dist(cm) Direction(wasd)\n");

	char* userStr;
	char ch;
	
	while(!exitFlag) {
		//auto == less latency(because of getch()), less control
		//manual == more latency(because of fgets + string manipulation), more control 
		if (manFlag == 0)
		{
			ch = getch();
			ch = tolower(ch);
			sendCommand(&ch);
		}
		else 
		{
			userStr = fgets(userStr,MAX_STR_LEN,stdin);
			sendCommand(userStr);
		}
	}

	printf("Closing connection to Arduino.\n");
	endSerial();
}
