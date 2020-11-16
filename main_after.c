#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

// Logging levels - flip flags if more output is required
#define LOGGING_VERBOSE 0
#define LOGGING_INFO LOGGING_VERBOSE || 0

// Server crequest statuses
#define STATUS_CONN_INACTIVE   0
#define STATUS_CONN_WRITE      1
#define STATUS_CONN_READ       2
#define STATUS_CONN_SHUTDOWN   3

//
// It is quite usual for clients and servers to 
// store data differently - use this to simulate
// a Y2K bug
//
// Fixed to represent them in a way that takes 
// each other into account
//
#define SERVER_FORMAT "%.2d.%.2d.%.2d"
#define CLIENT_FORMAT "%.2d.%.2d.%.4d"

// Network over which we will comunicate
struct Network net;
pthread_mutex_t netLock;
pthread_mutex_t stdioLock;

// Represents a Date
// Use int instead of char for year
struct Date {
    unsigned char day;
    unsigned char month;
    unsigned int year;
};

//
// Simulates a network. Statuses correspond to commands to the server: 
//            - STATUS_CONN_INACTIVE = Connection is idle
//            - STATUS_CONN_WRITE = Update the date on the server
//            - STATUS_CONN_READ = Read the date from the server
//            - STATUS_CONN_SHUTDOWN = Shut server down
//
struct Network {
    int status;
    struct Date date;
};

// Helper that creates a date
// Use int instead of char for year
struct Date CreateDate (unsigned char day, unsigned char month, unsigned int year) {
    struct Date date;
    date.day = day;
    date.month = month;
    date.year = year;
    return date;
}

//
// Helper that reads a date from stdin
// Apparently on GCC, printf/scanf have to be
// guarded with mutexes to prevent weird output
//
// Use the proper data types for the year to
// prevent clipping it to some weird value
//
struct Date ReadDate () {
    pthread_mutex_lock(&stdioLock);
    struct Date date;
    unsigned int d, m, y;
    printf("Day (dd): "); 
    scanf("%d", &d);
    printf("Month (mm): "); 
    scanf("%d", &m);
    printf("Year (yyyy): "); 
    scanf("%d", &y);
    pthread_mutex_unlock(&stdioLock);
    date = CreateDate(d, m, y);
    return date;
}

// Transforms a Date structure into a c-string according to format provided
char * DateToString (struct Date date, char * format) {
    char * buffer = (char *) malloc((strlen(format) + 1) * sizeof(char));
    sprintf(buffer, format, date.day, date.month, date.year);
    return buffer;
}

// Simulates accepting a request from a client
int Accept (struct Network * net) {
    int status = net->status;
    if (status == STATUS_CONN_WRITE){
        if (LOGGING_VERBOSE) printf("Write\n");
        if (LOGGING_INFO) printf("Date updated on server: %s\n", DateToString(net->date, SERVER_FORMAT));
        pthread_mutex_lock(&netLock);
        net->status = STATUS_CONN_INACTIVE;
        pthread_mutex_unlock(&netLock);
    } else if (status == STATUS_CONN_READ){
        if (LOGGING_VERBOSE) printf("Read\n");
        if (LOGGING_INFO) printf("Date stored on server: %s\n", DateToString(net->date, SERVER_FORMAT));
        pthread_mutex_lock(&netLock);
        net->status = STATUS_CONN_INACTIVE;
        pthread_mutex_unlock(&netLock);
    } else if (status == STATUS_CONN_INACTIVE) {
        // Connection is idle
    } else {
        if (LOGGING_VERBOSE) printf("Exit command received\n");
    }
    return status;
}

// Runs the server loop - meant to run in its own thread
void *RunServer(void *vargp) {
    struct Network * net = (struct Network*) vargp;
    if (LOGGING_INFO) printf("Server started\n");

    // Continually accept requests until told to shut down
    int status;
    do {
        status = Accept(net);
    } while (status < STATUS_CONN_SHUTDOWN);
    if (LOGGING_INFO) printf("Server exited\n");
}

// Sends a command to the server, optionally with some data
void SendCommand (struct Network * net, int command, void * data) {
    pthread_mutex_lock(&netLock);
    net->status = command;
    if (command == STATUS_CONN_WRITE) {
       net->date = *(struct Date *) data;
    } 
    pthread_mutex_unlock(&netLock);
}

// Helper figures out which day of the week it is based on the date
// No longer assumes century is 19**
int GetDayOfWeekNumeric (struct Date date) {
    int y = date.year;
    int m = date.month;
    int d = date.day;
    int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    y -= m < 3;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

// Helper converts ordinal day of the week into a c-string
char * GetDayOfWeek (int which) {
   char * names[7] = {
       "Sunday",
       "Monday",
       "Tuesday",
       "Wednesday",
       "Thursday",
       "Friday",
       "Saturday"
   };
   return names[which];
}

// Client helper simulating a transaction with the end user
void BuyTicket (struct Date date) {
    int dow = GetDayOfWeekNumeric(date);
    char * cDow = GetDayOfWeek(dow);
    double fare = 1.0f;
    double rates[7] = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f};
    printf("\nWelcome! Today it's %s\n", DateToString(date, CLIENT_FORMAT));
    printf("NOTICE: Parking is free on Sunday, 50%% off on Saturday, and %.2f$ otherwise.\n", fare);
    printf("Day of the week is %s. Price=%.2f$\n\n", cDow, rates[dow]);
}

// 
// Runs a UI loop - a simple numeric menu interacting with the server via the simulated 
// Network - until told to exit by the user
//
void *RunClient(void *vargp) {
    struct Network * net = (struct Network*) vargp;
    struct Date date;
    int ans;
    do {
        printf("1 - Buy parking ticket\n2 - Get server date (Admin)\n3 - Update server date (Admin)\n4 - Quit\n");
        scanf("%d", &ans);
        switch (ans) {
            case 1:
                SendCommand(net, STATUS_CONN_READ, &date);
                BuyTicket(net->date);
                break;
            case 2:
                SendCommand(net, STATUS_CONN_READ, &date);
                printf("Date: %s\n", DateToString(net->date, CLIENT_FORMAT));
                break;
            case 3:
                date = ReadDate();
                SendCommand(net, STATUS_CONN_WRITE, &date);
                printf("Date: %s\n", DateToString(net->date, CLIENT_FORMAT));
                break;
        }
    } while (ans < 4);
    if (LOGGING_INFO) printf("Client exited\n");            
    SendCommand(net, STATUS_CONN_SHUTDOWN, NULL);
} 

// Helper to hold the CMD window open
void Pause () {
    printf("Press any key to continue...\n");
    getch();
}

int main() {

    net.status = STATUS_CONN_INACTIVE;
    // Input the full 1999 instead of 99
    net.date = CreateDate(31, 12, 1999);
    pthread_mutex_init(&netLock, NULL);
    pthread_mutex_init(&stdioLock, NULL);

    // Run the server and client in separate threads so we can 
    // simulate them communicating over a network
    pthread_t serverThreadId, clientThreadId;

    pthread_create(&serverThreadId, NULL, RunServer, (void *)&net);
    pthread_create(&clientThreadId, NULL, RunClient, (void *)&net);

    pthread_join(clientThreadId, NULL);
    pthread_join(serverThreadId, NULL);

    pthread_mutex_destroy(&netLock);
    pthread_mutex_destroy(&stdioLock);

    Pause();
    pthread_exit(NULL);
    
    return 0;
} 