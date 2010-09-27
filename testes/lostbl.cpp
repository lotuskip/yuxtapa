// Please see LICENSE file.
#include <iostream>
#include <cmath>
#include <vector>
#include <utility>

using namespace std;

void swap(short &a, short &b)
{
	short tmp = b;
	b = a;
	a = tmp;
}


void interpolate(short x0, short y0, short x1, short y1, vector< pair<char,char> > &res)
{
	res.clear();
	bool steep = abs(y1 - y0) > abs(x1 - x0);
	if(steep)
	{
		swap(x0, y0);
		swap(x1, y1);
	}
	if(x0 > x1)
	{
		swap(x0, x1);
		swap(y0, y1);
	}
	int deltax = x1 - x0;
	int deltay = abs(y1 - y0);
	int error = deltax/2;
	int ystep;
	int y = y0;
	if(y0 < y1)
		ystep = 1;
	else
		ystep = -1;
	for(int x = x0; x <= x1; ++x)
	{
		if(steep)
			res.push_back(make_pair(y,x));
		else
			res.push_back(make_pair(x,y));
		if((error -= deltay) < 0)
		{
			y += ystep;
			error += deltax;
		}
	}
}


void print_res(vector< pair<char,char> > &res)
{
	if(res.front().first == 0 && res.front().second == 0)
	{
		vector< pair<char,char> >::const_iterator it;
		// print all but first:
		for(it = res.begin()+1; it != res.end(); ++it)
		{
			cout << int(it->first) << ", " << int(it->second);
			if(it != res.end()-1)
				cout << ",/**/ ";
		}
	}
	else
	{
		vector< pair<char,char> >::reverse_iterator rit;
		// print in reverse order all but last
		for(rit = res.rbegin()+1; rit != res.rend(); ++rit)
		{
			cout << int(rit->first) << ", " << int(rit->second);
			if(rit != res.rend()-1)
				cout << ",/**/ ";
		}
	}
	cout << ',' << endl;
}


int main()
{
	short x, y, r;
	int num = 0;
	vector< pair<char,char> > res;
	for(r = 2; r <= 11; r++)
	{
		cout << "const char pointlu" << r  << "[] = {" << endl;
		for(x = -r; x <= r; ++x)
		{
			cout << "/*(" << x << ",-" << r << "):*/ ";
			interpolate(x, -r, 0, 0, res);
			num += res.size();
			print_res(res);
		}
		for(y = -r+1; y <= r; ++y)
		{
			cout << "/*(" << r << "," << y << "):*/ ";
			interpolate(r, y, 0, 0, res);
			num += res.size();
			print_res(res);
		}
		for(x = r-1; x >= -r; --x)
		{
			cout << "/*(" << x << "," << r << "):*/ ";
			interpolate(x, r, 0, 0, res);
			num += res.size();
			print_res(res);
		}
		for(y = r-1; y > -r; --y)
		{
			cout << "/*(-" << r << "," << y << "):*/ ";
			interpolate(-r, y, 0, 0, res);
			num += res.size();
			print_res(res);
		}
		cout << "};" << endl;
		cerr << "Amount of data is " << num*2 << " bytes, " << num*2/1024 << "kb" << endl;
	}
	return 0;
}

