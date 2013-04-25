#include <fstream>
#include <iostream>
#include <string>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;

//void collateOutputs()
int main()
{
  ifstream fin;
  ofstream fout;
  string filepath, dir, outputfp, line, name;
  DIR *dp;
  struct dirent *dirp;
  struct stat filestat;
  int count;

  count = 0;
  dir = "output";

  dp = opendir( dir.c_str() );
  if (dp == NULL)
    {
      cout << "Error(" << ") opening " << dir << endl;
      return 1;
    }

  outputfp = dir + "/output.csv";
  fout.open( outputfp.c_str(), ios::out );
  while ((dirp = readdir( dp )))
    {
      name = dirp->d_name;

      if(dirp->d_name[0] == '.')
	continue;

      if(name == "output.csv")
	continue;

      count++;

      filepath = dir + "/" + dirp->d_name;

      cout << "Reading file : " << filepath << "\n";

      // If the file is a directory (or is in some way invalid) we'll skip it 
      if (stat( filepath.c_str(), &filestat )) continue;
      if (S_ISDIR( filestat.st_mode ))         continue;

      // Endeavor to read a single number from the file and display it
      fin.open( filepath.c_str() );
      while (fin.good())
      {
	getline ( fin,line );
	fout << line << endl;
      }
      fin.close();
      
      remove(filepath.c_str());
    }

  closedir( dp );
  fout.close();
  cout << "Found " << count << " files to process\n";

  return 0;

}
