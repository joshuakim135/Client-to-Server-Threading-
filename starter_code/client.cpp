#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"

#include <thread>
#include <sys/wait.h>

using namespace std;

struct Response {
    int person;
    double ecg;
};

void patient_thread_function(int p, int n, BoundedBuffer* request_buffer){
    DataRequest d(p, 0.0, 1);
    for(int i = 0; i < n; i++) {
        request_buffer->push((char*) &d, sizeof(DataRequest));
        d.seconds += 0.004;
    }
}

void file_helper(string revName, string filename, char* buf, FIFORequestChannel* chan) {
    FileRequest f(0,0);
    memcpy(buf, &f, sizeof(f));
    strcpy(buf + sizeof(f), filename.c_str());
    chan->cwrite(buf, sizeof(f) + filename.size() + 1);
}

void file_helper2(string recvName, int64_t filelength) {
    FILE* fp = fopen(recvName.c_str(), "w");
    fseek(fp, filelength, SEEK_SET);
    fclose(fp);
}

void file_thread_function(string filename, BoundedBuffer* request_buffer, FIFORequestChannel* chan, int bufcap) {
    // same as PA 3
    string recvName = "received/" + filename;
    char buf[1024];
    file_helper(recvName, filename, buf, chan);
    int64_t filelength;
    chan->cread(&filelength, sizeof(filelength));

    file_helper2(recvName, filelength);
    FileRequest* fr = (FileRequest*) buf;
    int64_t rem = filelength;
    while(rem > 0) {
        fr->length = min(rem, (__int64_t) bufcap);
        request_buffer->push(buf, sizeof(FileRequest) + filename.size() + 1);
        fr->offset += fr->length;
        rem -= fr->length;
    }
}

void worker_thread_function(BoundedBuffer* request_buffer, FIFORequestChannel* wchan, BoundedBuffer* response_buffer, int bufcap){
    char buf [1024];
    char recvbuf [bufcap];

    while(true) {
        request_buffer->pop(buf, sizeof(buf));
        REQUEST_TYPE_PREFIX* m = (REQUEST_TYPE_PREFIX* ) buf;
        if(*m == QUIT_REQ_TYPE) {
            wchan->cwrite(m, sizeof(REQUEST_TYPE_PREFIX));
            delete wchan;
            break;
        }
        if(*m == DATA_REQ_TYPE) {
            DataRequest* d = (DataRequest*) buf;
            wchan->cwrite(d, sizeof(DataRequest));
            double ecgVal;
            wchan->cread(&ecgVal, sizeof(double));
            Response r{d->person, ecgVal};
            response_buffer->push((char*)&r, sizeof(r));
        } else if (*m == FILE_REQ_TYPE) {
            FileRequest* fr = (FileRequest*) buf;
            string fname = (char*)(fr + 1);
            int sz = sizeof(FileRequest) + fname.size() + 1;
            wchan->cwrite(buf, sz);
            wchan->cread(recvbuf, bufcap);

            string recvfname = "received/" + fname;
            FILE* f = fopen(recvfname.c_str(), "r+");
            fseek(f, fr->offset, SEEK_SET);
            fwrite(recvbuf, 1, fr->length, f);
            fclose(f);
        }
    }
}

void histogram_thread_function (BoundedBuffer* response_buffer, HistogramCollection* hc) {
    char buf [1024];
    while(true) {
        response_buffer->pop(buf, 1024);
        Response* r = (Response*) buf; // this should be correct
        if(r->person == -1) {
            break;
        }
        hc->update(r->person, r->ecg);
    }
}

int main(int argc, char *argv[]) {
    int opt;
    int n = 100; // default number of requests per patient
    int p = 15; // default number of patients
    int w = 100; // default number of worker threads
    int b = 1024; // default cap of Bounded buffers
	int m = MAX_MESSAGE; // default cap of message buffer
    int h = 5;
    string filename = "";
    bool fileReq = false;
    
    while((opt = getopt(argc, argv, "n:p:w:b:m:f:h:")) != -1) {
        switch (opt) {
            case 'n':
                n = atoi(optarg);
                break;
            case 'p':
                p = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 'm':
                m = atoi(optarg);
                break;
            case 'f':
                filename = optarg;
                fileReq = true;
                break;
            case 'h':
                h = atoi(optarg);
                break;
            
        }
    }

    int pid = fork ();
	if (pid < 0){
		EXITONERROR ("Could not create a child process for running the server");
	}
	if (!pid) { // The server runs in the child process
		char serverName[] = "./server";
		char* args[] = {"./server", "-m",(char*)to_string(m).c_str(), nullptr};
		
		if (execvp(args[0], args) < 0){
			EXITONERROR ("Could not launch the server");
		}
	}
    
	FIFORequestChannel* chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer requestBuffer(b);
    BoundedBuffer responseBuffer(b);
	HistogramCollection hc;

    thread patients [p];
    thread workers [w];
    thread hists [h];
    FIFORequestChannel* wchans [w];

    // create channels
    for(int i = 0; i < w; i++) {
        REQUEST_TYPE_PREFIX m = NEWCHAN_REQ_TYPE;
        chan->cwrite(&m, sizeof(m));
        char newchanname [100];
        chan->cread(newchanname, sizeof(newchanname));
        wchans [i] = new FIFORequestChannel(newchanname, FIFORequestChannel::CLIENT_SIDE);
    }

    if(filename == "") { // initialize histogram
        for(int i =0; i < p; i++) {
            Histogram* hist = new Histogram(10, -2.0, 2.0);
            hc.add(hist);
        }
    }
    
    struct timeval start, end;
    gettimeofday (&start, 0);

    /* 1. Start all threads here */
    if(!fileReq) { // Data Request
        for(int i = 0; i < p; i++) {
            patients[i] = thread(patient_thread_function, i+1, n, &requestBuffer);
        }
        for(int i = 0; i < w; i++) {
            workers[i] = thread(worker_thread_function, &requestBuffer, wchans[i], &responseBuffer, m);
        }
        for(int i = 0; i < h; i++) {
            hists[i] = thread(histogram_thread_function, &responseBuffer, &hc);
        }
    }
    else { // 2. file req
        thread filethread(file_thread_function, filename, &requestBuffer, chan, m);
        for(int i = 0; i < w; i++)
            workers[i] = thread(worker_thread_function, &requestBuffer, wchans[i], &responseBuffer, m);
        filethread.join();
    }
    
    
	/* 2. Join all threads here */
    if (!fileReq) { // join patients
        for(int i = 0; i<p; i++)
            patients[i].join();
    }

    // 3/4. send quit msg to worker threads and join them
    for (int i = 0; i<w; i++) {
        REQUEST_TYPE_PREFIX q = QUIT_REQ_TYPE;
        requestBuffer.push((char*) &q, sizeof(REQUEST_TYPE_PREFIX));
    }
    for (int i = 0; i<w; i++)
        workers[i].join();

    // 5/6. get rid of histogram threads and join them
    if (!fileReq) {
        // break condition for hist threads
        Response r {-1, 0}; 
        for(int i = 0; i<h; i++)
            responseBuffer.push((char*) &r, sizeof(r));
        for(int i = 0; i<h; i++)
            hists[i].join();
    }
    
    gettimeofday (&end, 0);

    // print the results
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    std::cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    REQUEST_TYPE_PREFIX q = QUIT_REQ_TYPE;
    chan->cwrite ((char *) &q, sizeof (REQUEST_TYPE_PREFIX));
    wait(0);
    delete chan;
}
