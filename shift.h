#pragma once

#include "hls_stream.h"

template<typename T, int D, int C>
void _relu(hls::stream<T>& fmap, hls::stream<T>& act){
	for(int i=0;i<D;i++){
		for(int j=0;j<D;j++){
			for(int k=0;k<C;k++){
#pragma HLS PIPELINE
				T diff = fmap.read();
				if(diff < 0)
					act.write(0);
				else
					act.write(diff);
			}
		}
	}
}

template<typename T, int D, int C, int S>
void _shift_3x3(hls::stream<T>& fmap, hls::stream<T>& act,
		const int Dx[C],
		const int Dy[C]){
	static const int nD = (D - 1)/S + 1;
	T buffer[2][D][C];
#pragma HLS ARRAY_PARTITION variable=buffer complete dim=1
#pragma HLS ARRAY_PARTITION variable=buffer complete dim=2
	for(int i=0;i<D;i++)
		for(int j=0;j<C;j++){
#pragma HLS PIPELINE
			T r = fmap.read();
			buffer[0][i][j] = r;
			buffer[1][i][j] = 0;
		}
	for(int i=0;i<D;i++){
		for(int j=0;j<D;j++){
			for(int k=0;k<C;k++){
#pragma HLS PIPELINE
				T r,w;
				if(i != D -1)
					r = fmap.read();
				else
					r = 0;

				int ci = i & 0x01;
				int ni = ci ^ 0x01;

				switch(Dy[k]){
					case 0:
						switch(Dx[k]){
							case 0:
								w = buffer[ci][j][k];
								buffer[ni][j][k] = r;
								break;
							case 1:
								w = buffer[ni][j][k];
								buffer[ni][j][k] = r;
								break;
							case -1:
								w = r;
								break;
						}
						break;
					case 1:
						buffer[ni][j][k] = r;
						if(j == 0) {
							w = 0;
						} else
							w = buffer[ci][j - 1][k];
						break;
					case -1:
						buffer[ni][j][k] = r;
						if(j == D - 1) {
							w = 0;
						} else
							w = buffer[ci][j + 1][k];
						break;
				}
				if(i % S == 0 && j % S ==0)
					act.write(w);
			}
		}
	}
}

template<typename T, int D, int C, int N, int S>
void _conv2d_1x1(hls::stream<T> &fmap, hls::stream<T> &out, const T p[C][N]){

	T sum[N];
#pragma HLS ARRAY_PARTITION variable=sum complete dim=1

	for(int i=0;i<N;i++)
#pragma HLS PIPELINE
		sum[i] = 0;

	int is =0, js =0;
	bool skip = false;
	for(int i=0, is =0;i<D;i++, is++){
		for(int j=0, js=0;j<D;j++, js++){
			if(is == S) is =0;
			if(js == S) js =0;
			if(is ==0 && js ==0)
				skip = false;
			else
				skip = true;

			for(int k=0;k<C;k++){
#pragma HLS PIPELINE
				T tmp = fmap.read();
				if(skip)
					continue;
				for(int n=0;n<N;n++)
#pragma HLS UNROLL
					sum[n] += p[k][n] * tmp;
			}
			if(skip)
				continue;
			for(int n=0;n<N;n++){
#pragma HLS PIPELINE
				out.write(sum[n]);
				sum[n] = 0;
			}
		}
	}
}


template<typename T, int D, int C, int S>
void _max_pool(hls::stream<T> &fmap, hls::stream<T> &act){

	static const int nD = D/S;
	T buffer[nD][C];
	int c =0, is =0, js =0;
	for(int i = 0;i<D;i++, is++)
		for(int j = 0, js = 0, c = 0;j<D;j++, js++)
			for(int k = 0;k<C;k++){
#pragma HLS PIPELINE
				if(is == S) is = 0;
				if(js == S) {
					js = 0;
					c++;
				}
				if(c == nD){
					fmap.read();
					continue;
				}
				T r = fmap.read();
				T cmp = buffer[c][k];
				if((is == 0 && js ==0) || cmp < r){
					buffer[c][k] = r;
					cmp = r;
				}
				if( is == S - 1 && js == S -1)
					act.write(cmp);
			}
}

template<typename T, int D, int C1, int C2>
void _concat(hls::stream<T> & in1, hls::stream<T>& in2, hls::stream<T> &out){
	for(int i=0;i<D;i++)
		for(int j=0;j<D;j++){
			for(int k = 0; k<C1;k++)
#pragma HLS PIPELINE
				out.write(in1.read());
			for(int k = 0; k<C2;k++)
#pragma HLS PIPELINE
				out.write(in2.read());
		}
}

template<typename T, int D, int C>
void _duplicate(hls::stream<T> & in, hls::stream<T>& out1, hls::stream<T> &out2){
	for(int i=0;i<D;i++)
		for(int j=0;j<D;j++)
			for(int k = 0; k<C;k++){
#pragma HLS PIPELINE
				T r = in.read();
				out1.write(r);
				out2.write(r);
		}
}

template<typename T, int D, int D_shift, int S_conv, int S_pool, int IP, int MP, int OP>
void _shift(hls::stream<T> & input,
		hls::stream<T> & output,
		const int Dx[MP],
		const int Dy[MP],
		const T p0[IP][MP],
		const T p1[MP][OP]
		){
	static const int sD = (D - 1)/D_shift + 1;
	static const int cD = (sD - 1)/S_conv + 1;
	static const int mD = (cD - 1)/S_pool + 1;
#pragma HLS INLINE
		hls::stream<DataType> f_conv0, f_shift,f_conv1, f_pool;
#pragma HLS STREAM variable=f_shift depth=1 dim=1
#pragma HLS STREAM variable=f_conv0 depth=1 dim=1
#pragma HLS STREAM variable=f_pool depth=1 dim=1
#pragma HLS STREAM variable=f_conv1 depth=1 dim=1

		_conv2d_1x1<DataType,D, IP, MP, 1>(input, f_conv0, p0);
		_shift_3x3<DataType, D, MP, D_shift>(f_conv0, f_shift, Dx, Dy);
		_conv2d_1x1<DataType,sD, MP, OP, S_conv>(f_shift, f_conv1, p1);
		_max_pool<DataType, cD, OP, S_pool>(f_conv1,f_pool);
		_relu<DataType, mD, OP>(f_pool, output);
}
