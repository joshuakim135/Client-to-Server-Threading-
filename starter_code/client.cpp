#include "common.h"
#include "FIFOreqchannel.h"
#include "BoundedBuffer.h"
#include "HistogramCollection.h"
#include <sys/wait.h>
using namespace std;

void patient_thread_function(/*add necessary arguments*/){
    /* What will the patient threads do? */
}

void worker_thread_function(/*add necessary arguments*/){
    /*
		Functionality of the worker threads	
    */
}
void histogram_thread_function (/*add necessary arguments*/){
    /*
		Functionality of the histogram threads	
    */
}
int main(int argc, char *argv[]){

	int opt;
	int p = 1;
	double t = 0.0;
	int e = 1;
	string filename = "";
	int b = 10; // size of bounded buffer, note: this is different from another variable buffercapacity/m
	// take all the arguments first because some of these may go to the server
	while ((opt = getopt(argc, argv, "f:")) != -1) {
		switch (opt) {
			case 'f':
				filename = optarg;
				break;
		}
	}

	int pid = fork ();
	if (pid < 0){
		EXITONERROR ("Could not create a child process for running the server");
	}
	if (!pid){ // The server runs in the child process
		char* args[] = {"./server", nullptr};
		if (execvp(args[0], args) < 0){
			EXITONERROR ("Could not launch the server");
		}
	}
	FIFORequestChannel chan ("control", FIFORequestChannel::CLIENT_SIDE);
	BoundedBuffer request_buffer(b);
	HistogramCollection hc;


	struct timeval start, end;
    gettimeofday (&start, 0);

    /* Start all threads here */
	

	/* Join all threads here */
    gettimeofday (&end, 0);

    // print the results and time difference
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;
	
	// closing the channel    
    Request q (QUIT_REQ_TYPE);
    chan.cwrite (&q, sizeof (Request));
	// client waiting for the server process, which is the child, to terminate
	wait(0);
	cout << "Client process exited" << endl;

}
