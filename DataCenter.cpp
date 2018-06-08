//Data center simulation
//By Leighton Covington
//Originally created as a part of my Operating Systems coursework 


//To compile on ubuntu: g++ -w DataCenter.cpp -lpthread -o DataCenter

//To run: ./DataCenter


#include <unistd.h>
#include <stdio.h>      /* printf, fgets */
#include <stdlib.h>  /* atoi */
#include <iostream>
#include <fstream>
#include <vector>
#include <pthread.h>
#include <cstdlib>//used for random number generation
#include <semaphore.h>
#include <algorithm> //used when generating random numbers, checking if our list of random nubmers is already in the vector inspired by https://stackoverflow.com/questions/3450860/check-if-a-stdvector-contains-a-certain-object
// By the user You, I wanted something to quickly see if an element was already contained in a vector, and I found You's solution
#include <string>
#include <iostream>
#include <sstream>

using namespace std;

//Global Variables
bool terminateServers = false;//this variable is used by our main thread to terminate our data-server threads

int numberOfDataGenerators = 5;//variable storing how many data-generator threads we want
int numberOfDataServers = 2;//variable storing how many data-server threads we want

vector<int> serverBuffers(numberOfDataServers);//using the servers ID as an index - 1, this list stores the datum to be processed by the server
vector<int> serverClientList(numberOfDataServers);//using the servers ID as an index - 1, this list stores the ID of the client the server is serving
vector<int> availableServers(numberOfDataServers);//This list stores the ID's of the currently available servers

sem_t availableServer;//semaphore who limits DataGen threads access to the pool of servers, there may be only 1 thread inside their service section of their code for each server
sem_t modifyAvailableServerList;//semaphore to ensure that only one thread is modifying the available server list, Data Gens consume the first server from the list, Servers add themselves to the end of the list

vector<sem_t> dataGenConnectToServer(numberOfDataServers);//semaphore for the DataGen threads to signal to the server that they need help
vector<sem_t> dataGenHearBackFromServer(numberOfDataServers);//semaphore for the Server threads to signal to the DataGen threads that they are helping them
vector<sem_t> serverComplete(numberOfDataServers);//semaphore for the server threads to signal to the DataGen threads that they have finished processing their request

vector<string> serverLogs(numberOfDataServers);//this list keeps track of the data-server logs to be printed at the end of the program
vector<string> dataGenLogs(numberOfDataGenerators);//this list keeps track of the data-generator logs to be printed at the end of the program


void *DataGenerator(void* threadID){

    stringstream ss;//used to concatenate chars with long ints when we are creating the strings for the thread logs, I was running into problems using the << operator and long ints, this solution was inspired by
    //https://stackoverflow.com/questions/191757/how-to-concatenate-a-stdstring-and-an-int by the user DannyT who showed how stringstreams can be used to create strings from chars and ints

    long id = (long) threadID;//id of our thread
    vector<int> myRandomNumbers;//list to hold our 10 random numbers
    int numberOfDatumProcessed = 0;//keep count of how much data we have gotten the servers to process

    for(int i = 0; i < myRandomNumbers.size(); i++){//initialze our vector with values of zero
	myRandomNumbers[i] = 0;
    }

    for(int i = 0; i < 10; i++){//generate 10 random numbers

	bool isNewNumber = false;

	while(!isNewNumber){//while we don't have a new number
	    int random = (rand()%100) + (id*100);//generate a new number in the proper range
            if(find(myRandomNumbers.begin(), myRandomNumbers.end(), random) == myRandomNumbers.end()) {//if it is not in our list already -> inspired by a stack overflow post cited above by the #include <algorithm> answered by user You
    		myRandomNumbers.push_back(random);//add it to our list
	        isNewNumber = true;//we did find a new number stop looping
            }
        }
    }

    while(numberOfDatumProcessed < 10){//while we haven't had all of our data processed

	sem_wait(&availableServer);//wait for a server to be available

	sem_wait(&modifyAvailableServerList);//there is a server available, attain a lock to the server list
	int myServer = availableServers[0];//claim a server ID to be ours
	availableServers.erase(availableServers.begin());//remove that server ID from the available server list
	sem_post(&modifyAvailableServerList);//let someone else have access to the server list

	//since there is a 1:1 relationship between dataGen threads and server threads, and that there isn't a way for dataGen threads to get multiple server ID's IE dataGen threads already reserve their server in a
        //protected fashion the steps in this section don't need a semaphore, they use that server ID to index into these lists. It is noted however that there is potential risk here
	serverBuffers[myServer - 1] = myRandomNumbers[0];//set up our servers buffer to hold our data we want processed
	serverClientList[myServer - 1] = id;//set the server's client to us
	ss.str("");
        ss << "Data-generator thread " << id << " connects to data-server thread " << myServer << endl;
        dataGenLogs[id - 1] += ss.str();//add to our log
	//end of semaphoreless section

	sem_post(&dataGenConnectToServer[myServer - 1]);//signal our server that we need help, everything is set up for them to work

	sem_wait(&dataGenHearBackFromServer[myServer - 1]);//wait until they say they can help us
	ss.str("");
        ss << "Data-generator thread " << id << " receives the connection confirmation from data-server thread " << myServer << endl;
        dataGenLogs[id - 1] += ss.str();//add to our log

	sem_wait(&serverComplete[myServer - 1]);//wait until our server has completed our request
	ss.str("");
	ss << "Data-generator thread " << id << " acknowledges the completion of the number datum " << myRandomNumbers[0] << " from data-server thread " << myServer << endl;
	dataGenLogs[id - 1] += ss.str();//add to our log
	myRandomNumbers.erase(myRandomNumbers.begin());//erase one of our random numbers
	numberOfDatumProcessed += 1;//tally that we successfully got a number processed
    }

    return NULL;

}

void *DataServer(void* threadID){

    stringstream ss;//struct used to manipulate ints and strings, similar reasoning as the stringstream use above in the datagenerator thread

    long id = (long) threadID;


    while(true){

        int sleeptime = (rand()%151) + 50;//time in milliseconds that we will sleep

        sem_wait(&dataGenConnectToServer[id - 1]);//wait to receive a signal from whoever needs your help through your initial connection semaphore

	if(terminateServers){//after much trouble, this is the best way I could find that would allow the main thread to signal to the servers that it was time to close up
	    break;//I used to have the condition in the while loop, but more often than not the servers would still get blocked waiting for an intial connection, was a race condition if they would read the bool at the right time or not
	}//with this set up, the main thread can set a boolean, then signal the servers with their initial connection semaphores, and gurantee that they terminate

        int myClient = serverClientList[id - 1];//we read our client in, this is unprotected as this is set before we are signaled from the data generator
        int myData = serverBuffers[id - 1];// we read what our data to process is, unprotected with the same reasoning as above
        ss.str("");
        ss << "Data-server thread " << id << " accepts the connection from data-generator thread " << myClient << endl;
        serverLogs[id - 1] += ss.str();//add to our log

        ss.str("");
        ss << "Data-server thread " << id << " sends the connection confirmation to data-generator thread " << myClient << endl;
        serverLogs[id - 1] += ss.str();//add to our log
        sem_post(&dataGenHearBackFromServer[id - 1]);//confirm with our client

        usleep(sleeptime*1000); //convert microseconds to milliseconds
        //simulating data processing

        ss.str("");
        ss << "Data-server thread " << id << " completes processing number datum " << myData << " in " << sleeptime <<  " milliseconds and notifies data-generator thread " << myClient << endl;
        serverLogs[id - 1] += ss.str();//add to our log
        sem_post(&serverComplete[id - 1]);//let our client know that their data has successfully been processed

	//Some cleanup before we are ready to take on a new client
        serverClientList[id - 1] = -1;//flush our client
        serverBuffers[id - 1] = -1;//flush our buffer

        sem_wait(&modifyAvailableServerList);//wait to modify the server list
        availableServers.push_back(id);//add ourselves to the available server list
        sem_post(&modifyAvailableServerList);//let others modify the server list we are done with it

        sem_post(&availableServer);//let the world know we are ready to help again
    }
    return NULL;
}

int main(int argc, char** argv){


    sem_init(&availableServer, 0, numberOfDataServers);//limit number of threads dealing with servers to the number of servers
    sem_init(&modifyAvailableServerList, 0, 1);//only one thread may be modify the shared available server list 

    for(int i = 0; i < numberOfDataServers; i++){//each server gets its own set of semaphores for the communication process between itself and its client
        sem_init(&dataGenConnectToServer[i], 0, 0);//all of the values are set to zero, these semaphores ensure the proper order of operations being carried out
	sem_init(&dataGenHearBackFromServer[i], 0, 0);
	sem_init(&serverComplete[i], 0, 0);
    }

    for(long i = 1; i <= numberOfDataServers; i++){//add the id's to the available server list
	availableServers[i-1] = i;
    }

    for(int i = 0; i < numberOfDataServers; i++){//set the server buffers and client lists initially to an invalid value for ease of debugging
	serverBuffers[i] = -1;
	serverClientList[i] = -1;
    }

    for(int i = 0; i < numberOfDataServers; i++){//initialize our server logs
	serverLogs[i] = "";
    }
    for(int i = 0; i < numberOfDataGenerators; i++){//initialize our data generator logs
	dataGenLogs[i] = "";
    }

    long thread;
    pthread_t* dataGen_thread_handles;
    pthread_t* server_thread_handles;

    dataGen_thread_handles = (pthread_t*) malloc(numberOfDataGenerators*sizeof(pthread_t));
    server_thread_handles = (pthread_t*) malloc(numberOfDataServers*sizeof(pthread_t));

    for(thread = 1; thread <= numberOfDataServers; thread++){
	pthread_create(&server_thread_handles[thread-1], NULL, DataServer, (void*) thread);
    }

    for(thread = 1; thread <= numberOfDataGenerators; thread++){
	pthread_create(&dataGen_thread_handles[thread-1], NULL, DataGenerator, (void*) thread);
    }

    for(thread = 0; thread < numberOfDataGenerators; thread++){
	pthread_join(dataGen_thread_handles[thread], NULL);
    }

    terminateServers = true;//if we made it here, we have successfully joined all of our number generators, there for our server's job is complete, time to terminate

    for(int i = 0; i < numberOfDataServers; i++){//signal our servers to proceed from waiting, this time when they hit that if statement they will break out of their while loop and terminate
	sem_post(&dataGenConnectToServer[i]);
    }

    for(thread = 0; thread < numberOfDataServers; thread++){//join the servers
	pthread_join(server_thread_handles[thread], NULL);
    }

  cout << "Data-server Logs: " << endl;

    for(int i = 0; i < numberOfDataServers; i++){//print the server logs
	cout << "--------Report of Data-server " << i + 1 << "-----------------" << endl;
	cout << serverLogs[i] << endl;
	cout << "--------End of Report of Data_server " << i + 1 << "----------------" << endl << endl;
    }

  cout << "Data-generator Logs: " << endl;

    for(int i = 0; i < numberOfDataGenerators; i++){//print the data generator logs
	cout << "-----------Report of Data-generator "<< i + 1 << "-----------------" << endl;
	cout << dataGenLogs[i] << endl;
	cout << "-----------End of Report of Data-generator "<< i + 1 << "-----------------" << endl << endl;

    }

    free(dataGen_thread_handles);
    free(server_thread_handles);


}


