/* 
======================================================
 Copyright 2016 Liang Ma

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
======================================================
*
* Author:   Liang Ma (liang-ma@polito.it)
*
*----------------------------------------------------------------------------
*/

#define CL_HPP_ENABLE_EXCEPTIONS

// This should be used when cl.hpp from SDAccel works.
//#include <CL/cl.hpp>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <unistd.h>
#include "xcl2.hpp"
#include "para.h"
#include "loss.h"
using namespace std;
using namespace para;

namespace Params
{
	char *kernel_name="shift";     // -n
	char *binary_name=NULL;     // -a
}
void usage(char* name)
{
	cout<<"Usage: "<<name
		<<" -b opencl_binary_name"
		<<" -n kernel_name"
		<<endl;
}
int main(int argc, char** argv)
{
	int opt;
	bool flaga=false,flagn=false;
	while((opt=getopt(argc,argv,"n:b:"))!=-1){
		switch(opt){
			case 'n':
				Params::kernel_name=optarg;
				flagn=true;
				break;
			case 'b':
				Params::binary_name=optarg;
				flaga=true;
				break;
			default:
				usage(argv[0]);
				return -1;
		}
	}
	// Check the mandatory argument.
	if(!flaga) {
		usage(argv[0]);
		return -1;
	}
	ifstream ifstr(Params::binary_name);
	const string programString(istreambuf_iterator<char>(ifstr),
			(istreambuf_iterator<char>()));
	float input[BATCH][D][D][C] = {
#include "inputs_batch_32"
		//#include "t_im"
	};

	float out[N];
	float ref[BATCH][N] = {
		//#include "t_l1"
#include "outputs_batch_32"
		//#include "t_cifar"
	};
	int targets[BATCH]={
#include "targets_batch_32"
	};
	vector<float, aligned_allocator<float> > h_im(BATCH * D*D*C), h_out(BATCH * N);

	for(int b=0;b<BATCH;b++)
		for(int i=0;i<D;i++)
			for(int j=0;j<D;j++)
				for(int c=0;c<C;c++)
					h_im[b *D*D*C + i*D*C+j*C+c]=input[b][i][j][c];
	try
	{
		vector<cl::Platform> platforms;
		cl::Platform::get(&platforms);

		cl::Context context(CL_DEVICE_TYPE_ACCELERATOR);
		vector<cl::Device> devices=context.getInfo<CL_CONTEXT_DEVICES>();

		cl::Program::Binaries binaries(1, make_pair(programString.c_str(), programString.length()));
		cl::Program program(context,devices,binaries);
		try
		{
			program.build(devices);
		}
		catch (cl::Error err)
		{
			if (err.err() == CL_BUILD_PROGRAM_FAILURE)
			{
				string info;
				program.getBuildInfo(devices[0],CL_PROGRAM_BUILD_LOG, &info);
				cout << info << endl;
				return EXIT_FAILURE;
			}
			else throw err;
		}

		cl::CommandQueue commandQueue(context, devices[0]);

		cl::Kernel kernel(program,Params::kernel_name);
		auto kernelFunctor = cl::KernelFunctor<cl::Buffer,cl::Buffer>(kernel);

		cl::Buffer d_im(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
				sizeof(float)*h_im.size(), h_im.data());
		cl::Buffer d_out(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY,
				sizeof(float)*h_out.size(), h_out.data());

		std::vector<cl::Memory> inBufVec, outBufVec;
		inBufVec.push_back(d_im);
		outBufVec.push_back(d_out);

		clock_t start = clock();
		commandQueue.enqueueMigrateMemObjects(inBufVec,0);
		cl::EnqueueArgs enqueueArgs(commandQueue,cl::NDRange(1),cl::NDRange(1));
		cl::Event event = kernelFunctor(enqueueArgs,
				d_im,d_out
				);

		commandQueue.enqueueMigrateMemObjects(outBufVec,CL_MIGRATE_MEM_OBJECT_HOST);
		commandQueue.finish();
		event.wait();
		
		
		int cor = 0;
		for(int b=0; b < BATCH;b++){
			for(int k=0;k<N;k++){
				out[k]= h_out[b *N + k];
			}

			int ord = ord_max<N>(out);
			if(ord == targets[b])
				cor++;
		}
		cout << "The accuracy is " << (float)cor / (float)BATCH * 100.0f<< "%."<<endl;
		clock_t t = clock() - start;
		cout << "The execution lasts for "<< (float)t /CLOCKS_PER_SEC <<" s (CPU time)."<<endl;
	}
	catch (cl::Error err)
	{
		cerr
			<< "Error:\t"
			<< err.what()
			<< endl;

		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;

}
