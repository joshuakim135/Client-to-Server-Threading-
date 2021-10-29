#include "common.h"
#include "FIFOreqchannel.h"
#include "BoundedBuffer.h"
#include "HistogramCollection.h"
#include <sys/wait.h>
using namespace std;

/* Where to create new channels for worker Threads
- create all at once in main and pass each channel as an argument to the worker function
- could create a new channel at the beginning of the worker func, and call quit once done
*/

// Parameters: Patient number | Request Buffer ref | 
	  	// Number of requests n | other optional vals
void patient_thread_function(int p, int n, BoundedBuffer* request_buffer){
    /* What will the patient threads do? */ // just do ecg 1
	// 1 thread per patient
	// for each thread
		// create a data packet (instance of DataReq class)
		// push packet to request buffer
		// repeat a&b for the first n timestamps
	DataRequest d(p, 0.0, 1);
	for (int i = 0; i < n; i++) {
		// request_buffer->push(d, sizeof(DataRequest));
		d.seconds += 0.004;
	}
}

// Parameters: filename, filelenght, Req Buffer reference, buffer capacity
void file_thread_function() {
	// 1 thread if file transfer is requested
	// for the 1 thread
		// create a file request (instance of FileRequest with filename included)
		// Push packet to request buffer
		// Go back to a for all windows of the file
}

// Parameter: Request Buffer reference, Histogram Buffer reference
void worker_thread_function(/*add necessary arguments*/){
    // for each thread
		// (a) read from request buffer
		/* if a patient packet, then push response to histogram buffer buffer
			and go back to (a)*/
		// Else if file transfer, then write to file and go back to (a)
		// else if quit message, then exit thread/function

		// inside each if
			// (a) send message through designated channel to server (cwrite)
			// (b) receive response from server (cread)
		// HINT: Server, process request has similar set-up
		// fopen, fseek, 
}

// Param: Histogram Collection reference, Histogram Buffer reference, Optional
void histogram_thread_function (/*add necessary arguments*/){
	// as many threads as the -h flag
	// for each thread
		// read from histogram buffer
		// use the histogram update() function with the value popped from the histogram buffer
		// go back to a (i assume read from histogram buffer)
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

	// how to ensure quit is last
	// join patient and worker thread to make sure they are done and then pass quit
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
