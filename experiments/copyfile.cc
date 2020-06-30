#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>

// An experiment to learn the difference between using the Linux syscalls and 
// file streams for reading from and writing to files.

void copy_with_syscalls()
{
  std::cout<<"Starting copy with syscalls...\n";
  int size = 56;
  char buffer[size];
  int read_fd = open("file.txt", O_RDONLY);
  int write_fd = open("copied.txt", O_WRONLY | O_CREAT, S_IRWXU);  
  std::cout<<"read_fd: "<< read_fd << ", write_fd: " << write_fd << "\n";
  
  if (read_fd == -1 || write_fd == -1)
  {
    std::cout<<"Uh oh, the files did not open properly.\n";
    return;
  }

  lseek(read_fd, 0, SEEK_SET);
  read(read_fd, buffer, size);
  write(write_fd, buffer, size);
  close(read_fd);
  close(write_fd);
  std::cout<<"Finished.\n";
}

void copy_with_streams()
{
  std::cout<<"Starting copy with streams...\n";
  std::fstream input_file;
  input_file.open("file.txt");

  if (!input_file.is_open())
  {
    std::cout<<"Uh oh, the input file did not open properly.\n";
    return;
  }

  std::ofstream output_file;
  output_file.open("copied_streams.txt");
 
  if (!output_file.is_open())
  {
    std::cout<<"Uh oh, the output file did not open properly.\n";
    return;
  }

  std::string line;
  while(std::getline(input_file, line))
  {
    output_file << line << "\n";
  }
 
  input_file.close();
  output_file.close();
  std::cout<<"Finished.\n";
} 

int main()
{
  copy_with_syscalls();
  copy_with_streams();
}

 
