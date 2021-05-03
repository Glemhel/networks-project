Each client has NetworkInfo struct, which contains information about clients in the network that exist. 
Just for starting, just hardcoded ip addresses will be there. 
Then, struct FileInfo will contain information about the file that is to be recieved:
id of host which distributes this file
size of the file
number of chunks this file is divided into
int got 0/1 array for each chunk - is it recieved yet or not.

VERY NAIIVE SIMPLE APPROACH

While client has some files missing, it would repeatedly ask for each chunk each of it's neighbours.
If neighbour does not respond, it would try some time again eventually. If neighbour starts to send datagrams for the chosen
chunk, this client marks this chunck as processing already, and ignores other's peers messages about this chunck.


