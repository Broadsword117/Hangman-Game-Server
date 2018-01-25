#include<signal.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<pthread.h>
#include<time.h>

#define MAX_MESSAGE_SIZE 2000
#define MAX_CLIENTS 10

pthread_t consumer_threads[MAX_CLIENTS], producer_thread;
pthread_mutex_t leader_board_lock, request_que_lock;
pthread_cond_t cond_leader_board;
//Socket variables
int socket_desc, client_socket;
struct sockaddr_in server, client;
// struct to use size for inialisation of client socekt
socklen_t client_addr_size;
// Authorized user Data
char ** users;
char ** passwords;
// Hangman word data
char ** words;
char ** nouns;

int que_length = 0, current_high_scores = 0, file_size = 0, shutdown_threads = 0, users_connected = 0, read_leaderboard = 0;
// Id variables to monitor what thread is doing what
int id[MAX_CLIENTS];

struct request{
    int request_id; // Unique id for request
    int socket; // Client socket descriptor
    struct request * next;
};

struct request *first_request = NULL; // head of que
struct request *last_request = NULL; // tail of que

struct score {
    char * user;
    int games_won;
    int games_played;
};

int score_count = 0;
struct score ** scores;
char ** formatted_scores;

// Get a request from the que 
struct request * get_request() {
    //lock que
    pthread_mutex_lock(&request_que_lock);

    struct request * a_request;

    if (que_length > 0) {
        a_request = first_request;
        first_request = a_request->next;

        if (first_request == NULL) {
            last_request = NULL;
        }
        que_length --;
    } else {
        a_request = NULL;
    }
    pthread_mutex_unlock(&request_que_lock);
    return a_request;
}
// Simple delay function
void delay(int milli_seconds) {
    clock_t start_time = clock();
    while (clock() < start_time + milli_seconds) {
        ;
    }
}

// Add a request to the que
void add_request(int request_id, int socket) {
    struct request * a_request = (struct request*)malloc(sizeof(struct request));
    a_request->request_id = request_id;
    a_request->socket = socket;
    a_request->next = NULL;

    if (que_length == 0) {
        first_request = a_request;
        last_request = a_request;
    } else {
        last_request->next = a_request;
        last_request = a_request;
    }
    que_length ++;
}
//Creates a formatted string using data from a score struct and adds it to the double pointer array formatted_scores
void format_scores() {
    formatted_scores = (char **)malloc(score_count * sizeof(char *));
    char * str = (char *)malloc(2000 * sizeof(char *));
    
    for (int i = 0; i < score_count; i++) {
        sprintf(str, "\nPlayer - %s\nGames won - %i\nGames played - %i", scores[i]->user, scores[i]->games_won, scores[i]->games_played);
        formatted_scores[i] = (char *)malloc(strlen(str) * sizeof(char *));
        strcpy(formatted_scores[i], str);
    }
}
//
char ** get_leader_board() {
    while(read_leaderboard == 1){
        pthread_cond_wait(&cond_leader_board, &leader_board_lock); 
    }

    return formatted_scores;
}
//Utility function for swapping scores when sorting
void score_swap(struct score **score1, struct score **score2) {
    struct score temp = **score1;

    **score1 = **score2;
    **score2 = temp;
}
//Sort scores, using a bubble sort method
void sort_scores() {
    int swapped = 0, i, j;

    for (i = 0; i < score_count - 1; i++) {
        swapped = 0;
            
        for (j = 0; j < score_count - i - 1; j++) {
            if (scores[j]->games_won > scores[j+1]->games_won) {
                score_swap(&scores[j], &scores[j+1]);
                swapped = 1;
            }
            //If scores of user A are higher then user B
            if (scores[j]->games_won == scores[j+1]->games_won) {
                //If games played are equal sort user A and user B 
                if ((scores[j]->games_played == scores[j+1]->games_played)) {
                    if (scores[j]->user[0] > scores[j + 1]->user[0]) {
                        score_swap(&scores[j], &scores[j+1]);
                        swapped = 1;
                    } else {
                        if (scores[j]->games_played > scores[j + 1]->games_played) {
                            score_swap(&scores[j], &scores[j+1]);
                            swapped = 1;
                        }
                    }
                }
            }  
        }

        if (swapped == 0) {
            break;
        }
    }
}
//Adds a score to the scores double pointer
void add_score(char * user, int games_won, int games_played) {
    //Lock the function while adding the score
    pthread_mutex_lock(&leader_board_lock);
    int match = 0;
    read_leaderboard = 1;
    //If the score count is 0 then we don't have to look to see if a player already exists
    if (score_count == 0) {
        scores = (struct score **)malloc((score_count + 1) * sizeof(struct score *));
        scores[score_count] = (struct score *)malloc(sizeof(struct score *));
        scores[score_count]->user = user;
        scores[score_count]->games_won = games_won;
        scores[score_count]->games_played = games_played;
        score_count ++;
    
    } else {
        // If leaderboard entries are greater than 0 check to see if that user already has an entry
        if (score_count > 0) {      
            for (int i = 0; i < score_count; i++) {
                if ((strcmp(scores[i]->user, user)) == 0) {
                    scores[i]->games_won += games_won;
                    scores[i]->games_played += games_played;
                    match = 1;
                }
            }
            //If there isn't a match add a new entry
            if (match == 0) {
                scores = (struct score **)realloc(scores, (score_count + 1) * sizeof(struct score*));
                scores[score_count] = (struct score *)malloc(sizeof(struct score *));
                scores[score_count]->user = user;
                scores[score_count]->games_won = games_won;
                scores[score_count]->games_played = games_played;
                score_count ++;
            }
        }
    }
    
    sort_scores();
    format_scores();
    read_leaderboard = 0;
    pthread_cond_broadcast(&cond_leader_board);
    pthread_mutex_unlock(&leader_board_lock);
}
//Gets a random phrase from the list of phrases
char * get_phrase() {
    int max_number = 0;
    int random_num;
    char * return_phrase = (char *)malloc(sizeof(char*) * MAX_MESSAGE_SIZE);
    while(words[max_number] != NULL) {
        max_number ++;
    }
    random_num = rand() % (max_number - 0) + 0;

    strcat(return_phrase,words[random_num]);
    strcat(return_phrase," ");
    strcat(return_phrase,nouns[random_num]);

    return return_phrase;
}
// Utility function for loading a file and returning its contents
char ** load_file (char * filename) {
    file_size = 0;
    FILE *auth;
    int array_size = 1;
    char str[500];
    char msg[500];
    char ** temp_array;
    auth = fopen(filename, "r");
    
    if (auth == NULL) {
        sprintf(msg, "Could not open file, %s\n", filename);
        puts(msg);
    } else {
        sprintf(msg, "Loading file, %s", filename);
        puts(msg);
    }
        
    temp_array  = (char **)malloc(sizeof(char *) * array_size);
    //Keep reading lines from a file till there are none left to read
    while (1) {
        int status = fscanf(auth, "%s", str);
        if (status == 1) {
            temp_array[array_size - 1] = (char *)malloc(sizeof(char) * strlen(str));
            strcpy(temp_array[array_size - 1], str);
            array_size++;
            file_size++;
            temp_array  = (char **)realloc(temp_array ,sizeof(char *) * array_size);
        }
        if (status == EOF) {
                break;
        }
    }
    return temp_array;
}
//Process the Autentication.txt file and add it to a global double pointer array
void load_users() {
    char ** temp_array = load_file("Authentication.txt");

    int index_step = 0;
    users  = (char **)malloc((file_size) * sizeof(char *));
    passwords  = (char **)malloc((file_size) * sizeof(char *));

    for (int i = 3; i < file_size; i+=2) {
        users[index_step] = malloc(strlen(temp_array[i - 1]) * sizeof(char *));
        strcpy(users[index_step], temp_array[i - 1]);

        passwords[index_step] = malloc(strlen(temp_array[i]) * sizeof(char *));
        strcpy(passwords[index_step], temp_array[i]);

        index_step ++;
    }
    free(temp_array);
}
//Process the hangman_text.txt file and add it to a global double pointer array
void load_words (){
    char ** temp_array = load_file("hangman_text.txt");

    words  = (char **)malloc(file_size * sizeof(char *));
    nouns  = (char **)malloc(file_size * sizeof(char *));
    char * word = (char*)malloc(500 * sizeof(char));
    char * noun = (char*)malloc(500 * sizeof(char));
  
    for (int i = 0; i < file_size; i++) {
        sscanf(temp_array[i], "%[^,],%[^,]", word, noun);
        words[i] = (char *)malloc(strlen(word) * sizeof(char *));
        strcpy(words[i], word);
        
        nouns[i] = (char *)malloc(strlen(noun) * sizeof(char *));
        strcpy(nouns[i], noun);
    }
    free(temp_array);
}
//Check a clients username and login to make sure that combination exists
char * process_login(char * input){
    char * user = malloc(sizeof(char *) * 500);
    char * pass = malloc(sizeof(char *) * 500);
    int pass_match = 0, user_match = 0, index = 0;
    
    sscanf(input, "%[^,],%[^,]", user, pass);
    
    while (users[index] != NULL) {
        
        if (strcmp(user, users[index]) == 0) {
            user_match = 1;
            if (strcmp(pass, passwords[index]) == 0) {
                pass_match = 1;
                break;
            }
        }
        index ++;
    }
    if (user_match == 1 && pass_match == 1){
        return user;
    } else {
        return NULL;
    }
}
// Utility function for sending messages to a client socket
void send_message (int socket, char * str){
    if(send(socket , str , MAX_MESSAGE_SIZE , 0) < 0) {
        puts("Send failed");
    }  
}
//Consumer thread handler, this is the thread that handles a client till they disconnect
void *consumer_handler(void * args){
    int id = *((int *)args);
    int game_state = -1, got_request = 0, finished_sending_list = 0;
    int read_size, socket;
    char * phrase = NULL, * display_phrase = NULL;
    char * user = NULL;
    char * msg = (char *)malloc(1000 * sizeof(char));
    char * client_msg = (char *)malloc(MAX_MESSAGE_SIZE * sizeof(char));
    struct request * client = NULL;
    struct request * temp_request = NULL;

    sprintf(msg,"Thread %i waiting for work...", id);
    puts(msg);
    // Run while loop unless a flag for shutting down the thread is changed
    while(shutdown_threads == 0) {
        // Keep polling for a client
        temp_request = get_request();
        
        if (got_request == 0) {
            /*
            If the que returns a client copy that clients socket details to the thread
            and change the got_request flag so it stops trying to get a new client
            */
            if (temp_request != NULL) {
                client = (struct request*)malloc(sizeof(struct request));
                client = temp_request;
                socket = client->socket;
                sprintf(msg,"Thread %i handling client %i", id, client->request_id);
                puts(msg);
                got_request = 1;
            }
        }
        
        // Game states: -1 - login mode, 0 - menu, 1 - play game, 2 - leaderboard, 3 - exit
        if (got_request == 1) {
            
            if((read_size = recv(socket , client_msg , MAX_MESSAGE_SIZE , 0)) > 0 ) {
                // Login mode
                if (game_state == -1) {
                    char * temp_user = process_login(client_msg);
                    /*
                    process_login will return a NULL when there is no match if it returns
                    a NULL the client is sent a -3 which instructs it to disconnect if the 
                    return value isn't null then the clients name will be copied to the threads
                    memory and the game state will be moved to 0 (menu)
                    */
                    if (temp_user != NULL) {
                        user = (char *)malloc(strlen(temp_user) * sizeof(char));
                        strcpy(user, temp_user);
                        send_message(socket, "0");
                        game_state = 0;
                    } else {
                        send_message(socket, "-3");
                    }
                }

                if (game_state == 0) {
                    sprintf(msg,"Client %s is browsing the menu", user);
                    //Client opts to play a round if they send "1" 
                    if (strcmp(client_msg, "1") == 0) {
                        char * temp_phrase = get_phrase();
                        phrase = (char *)malloc(strlen(temp_phrase) * sizeof(char));
                        display_phrase = (char *)malloc(strlen(temp_phrase) * sizeof(char));

                        strcpy(phrase, temp_phrase);
                        strcpy(display_phrase, temp_phrase);
                        // Create the display phrase that we want the client to see before playing a round
                        for (int i = 0; i < strlen(display_phrase); i++) {
                            if (display_phrase[i] != ' ') {
                                display_phrase[i] = '_';
                            }
                        }

                        sprintf(msg,"Client %s is playing a game with the phrase \"%s\"", user, temp_phrase);
                        puts(msg);
                        send_message(socket, "1"); // Give the client permission to proceed to play a round
                        game_state = 1;
                    }
                    // Client opts to view the high scores list
                    if (strcmp(client_msg, "2") == 0) {
                        finished_sending_list = 0;
                        send_message(socket, "2"); // Give client permission to view the high scores
                        game_state = 2;
                        sprintf(msg,"Client %s is viewing high scores", user);
                        puts(msg); 
                    }
                    // Client quits
                    if (strcmp(client_msg, "3") == 0) {
                        send_message(socket, "3"); // Give client permission to close their application
                        read_size = 0;
                    }
                }
                // Client is playing a round
                if (game_state == 1) {
                    if (strcmp(client_msg, "win") == 0) {
                        add_score(user, 1, 1);
                        send_message(socket, "0");
                        game_state = 0;

                    } else if (strcmp(client_msg, "loss") == 0) {
                        add_score(user, 0, 1);
                        send_message(socket, "0");
                        game_state = 0;

                    } else {
                        // Update the display phrase that is sent to the client after every guess   
                        for (int i = 0; i < strlen(phrase); i++) {
                            if (phrase[i] == client_msg[0]) {
                                display_phrase[i] = phrase[i];
                            }
                        }
                        send_message(socket, display_phrase);
                    }

                }

                if (game_state == 2) {
                    if (score_count == 0) {
                        puts("No scores available");
                        send_message(socket, "0");
                        game_state = 0;
                    }
                    // send all the high scores to client 1 by 1
                    if (finished_sending_list == 0 && score_count > 0) {
                        for (int i = 0; i < score_count; i++) {
                            send_message(socket, get_leader_board()[i]);
                        }
                        send_message(socket ,"end"); // This lets the client know that there are no more scores to be sent
                        finished_sending_list = 1;
                    }
                    // If the list has finished being sent wait for the client response
                    if (finished_sending_list == 1) {
                        if (strcmp(client_msg, "0") == 0) {
                            send_message(socket, "0");
                            game_state = 0;
                        }
                    }
                }
            }
            // If the client disonncects reset thread to handle any new clients
            if (read_size <= 0) {
                sprintf(msg, "Client disconnected\nThread %i closing client %i socket....", id, client->request_id);
                puts(msg);
                shutdown(socket, 2);
                got_request = 0;
                users_connected --;
                game_state = -1;
                sprintf(msg, "Thread %i returned to pool", id);
                puts(msg);
            }            
        }
    }

    sprintf(msg, "Cleaning thread %i....", id);
    puts(msg);
    shutdown(socket, 2);
    free(user);
    free(phrase);
    free(display_phrase);
    free(msg);
    free(client_msg);
    free(client);
    delay(100000);

    return NULL;
}

void *producer_handler(void * args){
    int client_id = 0;
    puts("Producer thread waiting for connections...");

    while(1) {
        
        if (shutdown_threads == 1) {
            puts("Cleaning producer thread....");
            break;
        }
        
        if((client_socket = accept(socket_desc, (struct sockaddr *)&client,&client_addr_size)) > 0) {
                if (users_connected < MAX_CLIENTS) {
                    send_message(client_socket, "-1");
                    puts("Connection Accepted");
                    add_request(client_id, client_socket);
                    client_id ++;
                    users_connected ++;
                } else {
                    shutdown(client_socket, 2);
                    puts("Server full disconnecting client");
                    
                }          
        } 
    }
    return NULL;
}

// handle ctr+c event
void sigint_handler(int signo) {
    if (signo == SIGINT) {
        shutdown_threads = 1;
        printf("\nTerminate signal detected, closing program\n");

        shutdown(client_socket, 2);
        shutdown(socket_desc, 2);

        for (int i = 0; i < MAX_CLIENTS; i++ ) {
            if ((pthread_join(consumer_threads[i], NULL)) == 0) {
                delay(10000);
                printf("Worker thread %i successfully terminated\n", i);
            } else {
                printf("Worker thread %i could not terminated\n", i);
            }
        }

        if ((pthread_join(producer_thread, NULL)) == 0) {
            puts("Producer thread successfully terminated\n");
        } else {
            puts("Producer thread could not terminate");
        }
        
        printf("Exiting application\n");
        exit(EXIT_SUCCESS);
    }
}
// Assign port number
int get_port_no(char* input) {
    int num = 0;
    // Check if the array element is null
    if (input == NULL) {
        printf("\nPort number not entered, port number 12345 has been assigned\n");
        num = 12345;
    }
    if (input != NULL) {
        // check if the port number length is appropriate
        if (strlen(input) < 4 || strlen(input) > 5) {
            puts("Invalid port, number please use a number with a length between 4 and 5 digits");
            exit(EXIT_FAILURE);
        } else {
            // convert string to int
            for(int i = 0; i<strlen(input); i++){   
                num = num * 10 + ( input[i] - '0' );
            }
        }
    }
    return num;
}

void setup_socket(int port) {
    // set up socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    // create socket
    if (socket_desc < 0) {
        printf("Could not create socket\n");
        exit(EXIT_FAILURE);
    } else {
        printf("Socket created\n");
    }
    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET; // set up IPV4 address
    // broadcast on any available IP that belongs to the host machine
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);// set port address to program argument

    if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0) {
        perror("setsockopt failed");
    }
    // Bind the server to the socket
    if(bind(socket_desc,(struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("Error not bind socket\n");
        exit(EXIT_FAILURE);
    } else {
        printf("Address bound to socket\n");
    }
    if(listen(socket_desc, 30) < 0) {
        printf("Error listening on server socket\n");
    } else {
        printf("Waiting for connections.......\n");
    }
}

void setup_threads() {
    pthread_create(&producer_thread, NULL, &producer_handler, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        id[i] = i;
        pthread_create(&consumer_threads[i], NULL, &consumer_handler, (void*)&id[i]);
    }
}

int main(int argc, char*argv[]) {
    srand(time(NULL));
    signal(SIGINT, sigint_handler);
    setup_socket(get_port_no(argv[1]));
    load_users();
    load_words();
    setup_threads();

    while(1) {

    }

    return 0;
}