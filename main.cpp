#include <iostream>
#include <fstream>
#include <list>
#include <string>
#include <mpir.h>
#include <mpirxx.h>
#include <utility>
#include <algorithm>
#include <SDL.h>
#include <direct.h>
#include <vector>
#include <map>
#undef main

using namespace std;


#include <Windows.h>

Uint32 getpixel(SDL_Surface *surface, int x, int y)
{
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to retrieve */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch (bpp) {
    case 1:
        return *p;

    case 2:
        return *(Uint16 *)p;

    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
        else
            return p[0] | p[1] << 8 | p[2] << 16;

    case 4:
        return *(Uint32 *)p;

    default:
        return 0;       /* shouldn't happen, but avoids warnings */
    } // switch
}

struct result{
	double uncertainty;
	int x, y;
	bool isMine;
	result(double uncertainty, int y, int x, bool isMine){
		this->uncertainty = uncertainty;
		this->x = x; this->y = y; this-> isMine = isMine;
	}
};

bool resultCompare(result a, result b){
	if(a.uncertainty<b.uncertainty) return true;
	if(a.uncertainty>b.uncertainty) return false;
	return !a.isMine;
}

//we will now abstract into a solver class.
class solver{
public:
	int data[16][30];	//y and x. -1 = unknown. -2 = assume mine, -3 = assume no mine. 0-8 indicates # of mines.
	int prevData[16][30];
	double luckiness; int guesses;
	int width, height, mines;
private:
	int counted[16][30];	//complete when set to 0.

	bool solved;
	int dx[8], dy[8];
	
	string outmap;
	mpz_class combination[16][30], total;

	void reset(int v[16][30], int val){
		for(int i = 0; i < 16; i++) for(int j = 0; j < 30; j++) v[i][j] = val;
	}

	void reset(mpz_class v[16][30], mpz_class val){
		for(int i = 0; i < 16; i++) for(int j = 0; j < 30; j++) v[i][j] = val;
	}
public:
	string outmap2;
	solver(int w, int h, int m){
		solved = false; width = w; height = h; mines = m;
		int dxx[8] = {-1,0,1,1,1,0,-1,-1}, dyy[8] = {-1,-1,-1,0,1,1,1,0};
		for(int k = 0; k < 8; k++){ dx[k] = dxx[k]; dy[k] = dyy[k];}
		outmap = " X .-|+)WM@";
		outmap2 = " X 012345678";
		reset(data,-1);
		reset(prevData,-1);
	}
	void set(int y, int x, int v){
		if(y>=0&&y<height&&x>=0&&x<width) data[y][x] = v;
	}
	void display(){
		cout<<"-----------------------------------------------\n";
		for(int i = 0; i < height; i++){
			for(int j = 0; j < width; j++){
				cout<<outmap2[data[i][j]+3];
			}
			cout<<"\n";
		}
	}
	void basicOutput(list <int> toInfer, int remaining){
		char output[16][30];
		for(int i = 0; i < height; i++) for(int j = 0; j < width; j++){
			output[i][j] = outmap[data[i][j]+3];
		}
		for(list<int>::iterator i = toInfer.begin(); i != toInfer.end(); i++){
			int x = (*i)&1023, y = ((*i)>>10)&1023, v = (*i)>>20;
			output[y][x] = v + '0';
		}
		for(int i = 0; i < height; i++){
			for(int j = 0; j < width; j++){
				cout<<output[i][j];
			}
			cout<<"\n";
		}
	}
	void basicOutputInput(list <int> toInfer,int remaining){
		basicOutput(toInfer,remaining);
		cout<<"Remaining: "<<remaining<<"\n";
		//input test.
		int y,x,val;
		cin>>y>>x>>val;
		data[y][x] = val;
	}
	void fact(mpz_class *result, int operand){
		(*result) = 1;
		for(int i = 1; i <= operand; i++) (*result)*=i;
	}
	
	//solver module will return list of all squares, sorted by certainty, with their likelihood of being a mine or not.
	int scenarios;
	bool isEmpty(){
		for(int i = 0; i < height; i++){
			for(int j = 0; j < width; j++) if(data[i][j]!=-1) return false;
		}
		return true;
	}

	bool isPrevious(){
		for(int i = 0; i < height; i++){
			for(int j = 0; j < width; j++) if(data[i][j]!=prevData[i][j]) return false;
		}
		return true;
	}

	bool isAnon(int y, int x){
		//TODO
		return 0;
	}

	void countCombinations(list<int> &group,map<int,int> &values,map<int,map<int,int> > &mineCombo, list<int>::iterator position){
		//mine solving step 3. TODO
		//Takes a group. Uses recursion to find all valid combinations.
		//once a valid combination is found, it stores the occurence as a
		//count in values, and as a whole in mineCombo. It uses the global
		//"Counted[16][30]" during calculation, which is assumed to be precomputed.
		if(position==group.end()){
			//we have made a valid thing.
			int NumOfMines = 0;
			vector <int> combo;
			for(list<int>::iterator i = group.begin(); i != group.end(); i++){
				int x = (*i)&1023, y = ((*i)>>10)&1023;
				if(data[y][x]==-2){
					//it is a mine in this scenario
					NumOfMines++;
					combo.push_back((*i));
				}
			}
			if(values.count(NumOfMines)==0) values[NumOfMines] = 0;
			values[NumOfMines]++;
			for(int i = 0; i < combo.size(); i++){
				if(mineCombo[NumOfMines].count(combo[i])==0)mineCombo[NumOfMines][combo[i]] = 0;
				mineCombo[NumOfMines][combo[i]] ++;
			}
			return;
		}
		//not done yet. Try this as a mine.
		int x = (*position)&1023, y = ((*position)>>10)&1023;
		for(int value = -2; value != -4; value--){
			bool valid = true;
			data[y][x] = value;
			for(int k = 0; k < 8; k++){
				if(x+dx[k]<0||x+dx[k]>=width||y+dy[k]<0||y+dy[k]>=height) continue;
				if(data[y+dy[k]][x+dx[k]]<0) continue;
				counted[y+dy[k]][x+dx[k]] -= 1;
				//count neighbouring 
				int xx = x+dx[k], yy = y+dy[k];
				int nMines = 0;
				for(int kk = 0; kk < 8; kk++){
					if(xx+dx[kk]<0||xx+dx[kk]>=width||yy+dy[kk]<0||yy+dy[kk]>=height) continue;
					if(data[yy+dy[kk]][xx+dx[kk]] == -2) nMines++;
				}
				//check validity.
				int v = data[y+dy[k]][x+dx[k]];
				if(nMines>v || nMines+counted[y+dy[k]][x+dx[k]] < v) valid = false;
			}
			if(valid){
				//try the next recurse.
				list<int>::iterator j = position;
				j++;
				countCombinations(group,values,mineCombo,j);
			}
			data[y][x] = -1;
			//revert counted and data to original state.
			for(int k = 0; k < 8; k++){
				if(x+dx[k]<0||x+dx[k]>=width||y+dy[k]<0||y+dy[k]>=height) continue;
				if(data[y+dy[k]][x+dx[k]]<0) continue;
				counted[y+dy[k]][x+dx[k]] += 1;
			}
		}

	}

	mpz_class combosWithout(vector<map<int,int> > &combos, int remaining, int groups, int CurrentGroup, int AvoidGroup){
		//returns the number of combinations of everything except group AvoidGroup.
		//TODO
	}

	void finallySolve(vector<map<int,int> > &combos, vector<map<int,map<int,int> > > &mineCombos, vector<int> &anon, int anons, int groups){
		//last step, where values are added to combination array. TODO
		//It basically brute forces through the options.
		for(int i = 0; i < groups; i++){
			int remaining = anons;
			
			for(map<int,map<int,int> >::iterator combination = mineCombos[i].begin(); 
				combination != mineCombos[i].end(); combination++){
				int minesUsed = (*combination).first;
				mpz_class multiplier = combosWithout(combos,remaining,groups,0,i);

				//for this number of used mines in this group, add up how many ways each of the blocks can be a mine.

			}
		}
	}

	list <result> solve(bool display = false, bool display2 = false){
		scenarios = 0;
		total = 0;
		reset(combination,0);
		reset(counted,0);
		
		list <int> toInfer;
		list <result> retVal;
		
		if(isEmpty()){
			//strike random square, because we just started. 
			luckiness = 1.0; guesses = 0;
			retVal.push_back(result(0,rand()%height,rand()%width,false));
			return retVal;
		}

		if(isPrevious()){
			cout<<"Done.\n";
			return retVal;
		}
		for(int i = 0; i < height; i++){
			for(int j = 0; j < width; j++) prevData[i][j]=data[i][j];
		}
		
		//Analyse the situation. create inference list.
		int checked[16][30], mines, remaining = 0;	//0 indicates unchecked. 1 indicates checked.
		reset(checked,0);

		for(int i = 0; i < height; i++){
			for(int j = 0; j < width; j++){
				if(!checked[i][j]){
					//use bfs to add to list if infered.
					list <int> proc;
					proc.push_back((i<<10)|j);
					while(!proc.empty()){
						int x = proc.back()&1023, y = proc.back()>>10;
						proc.pop_back();
						if(checked[y][x]) continue; checked[y][x] = true;
						if(data[y][x]!=-1) continue; 

						bool shouldInfer = false;
						for(int k = 0; k < 8; k++){
							if(x+dx[k] <0 || x+dx[k] >= width || y+dy[k] < 0 || y+dy[k] >= height) continue;
							if(data[y+dy[k]][x+dx[k]] >=0) shouldInfer = true;
							proc.push_back(((y+dy[k])<<10)|(x+dx[k]));
						}

						if(shouldInfer) toInfer.push_back((toInfer.size()<<20)|(y<<10)|x);
						else remaining++;
					}
				}
			}
		}
		
		for(int i = 0; i < height; i++) for(int j = 0; j < width; j++){
			if(data[i][j] == -1) for(int k = 0; k < 8; k++){
				if(i+dy[k]>=0 && i+dy[k]<height && j+dx[k]>=0 && j+dx[k]<width)
					counted[i+dy[k]][j+dx[k]] += 1;
			}
		}
		findSolution(toInfer.begin(),toInfer.end(),toInfer.begin(),display);
		//generate list.
		
		if(total==0){ cout<<"IMPOSSIBRUUUUU\n"; return retVal;}
		for(int y = 0; y < height; y++) for(int x = 0; x < width; x++){
			if(data[y][x]!=-1) continue;
			mpq_class a;
			a = combination[y][x];
			a /= total;
			bool isMine;
			if(a>0.5){
				isMine = true;
				a = 1-a;
			}else isMine = false;
			retVal.push_back(result(a.get_d(),y,x,isMine));
		}
		retVal.sort(resultCompare);
		cout<<"Total scenarios: "<<scenarios<<"\n";
		return retVal;
	}

private:
	int	findSolution(list<int>::iterator i, list<int>::iterator end, list<int>::iterator begin,bool display){
		/*
		for(int i = 0; i < height; i++){
			for(int j = 0; j < width; j++){
				cout<<counted[i][j];
			}
			cout<<"\n";
		}
		//*/
		if(i==end){
			
			//now, store to combinations board. Count remaining, and count mines remaining, to calculate a multiplier. Then, re-iterate the list, applying logic.

			int unaccounted = 0, remaining = mines;
			for(int y = 0; y < height; y++){
				for(int x = 0; x < width; x++){
					if(data[y][x]==-1) unaccounted ++;
					if(data[y][x]==-2) remaining--;
				}
			}
			mpz_class multiplier = 0, multiplier2 = 0;	//2 is the one for every other cell.
			if(remaining<=unaccounted){
				fact(&multiplier,unaccounted);
				mpz_class operand = 0;
				fact(&operand,remaining);
				multiplier/=operand;
				fact(&operand,unaccounted-remaining);
				multiplier/=operand;
			}
			multiplier2 = multiplier*remaining;
			if(unaccounted!=0) multiplier2 /= unaccounted;
			//we have multiplier. Now apply to table.
			for(list<int>::iterator c = begin; c != end; c++){
				int x = (*c)&1023, y = ((*c)>>10)&1023;
				if(data[y][x]==-2){
					combination[y][x]+=multiplier;
				}
			}
			for(int a = 0; a < height; a++) for(int b = 0; b < width; b++){
				if(data[a][b]==-1) combination[a][b]+=multiplier2;
			}
			total+=multiplier;
			if(display){
				//we have found. Output now.
				cout<<"--------------\n";
				list <int> b;
				basicOutput(b,0);
				cout<<"--------------\n";
				cout<<multiplier.get_str()<<" is the number of combinations for this config.\n";
				cout<<multiplier2.get_str()<<" is the number of combinations for any other independent block. \n";
				cout<<"With "<<unaccounted<<" unaccounted and "<<remaining<<" unused mines\n";
			}
			scenarios++;
			
			return 1;
		
		}
		int solutions = 0;
		//assumes a currently valid state.
		int x = (*i)&1023, y = ((*i)>>10)&1023;
		//try both on and off.
		//try is a mine, and is not mine in a loop
		for(int value = -2; value != -4; value--){
			bool valid = true;
			data[y][x] = value;
			for(int k = 0; k < 8; k++){
				if(x+dx[k]<0||x+dx[k]>=width||y+dy[k]<0||y+dy[k]>=height) continue;
				if(data[y+dy[k]][x+dx[k]]<0) continue;
				counted[y+dy[k]][x+dx[k]] -= 1;
				//count neighbouring 
				int xx = x+dx[k], yy = y+dy[k];
				int nMines = 0;
				for(int kk = 0; kk < 8; kk++){
					if(xx+dx[kk]<0||xx+dx[kk]>=width||yy+dy[kk]<0||yy+dy[kk]>=height) continue;
					if(data[yy+dy[kk]][xx+dx[kk]] == -2) nMines++;
				}
				//check validity.
				int v = data[y+dy[k]][x+dx[k]];
				if(nMines>v || nMines+counted[y+dy[k]][x+dx[k]] < v) valid = false;
			}
			if(valid){
				//try the next recurse.
				list<int>::iterator j = i;
				j++;
				solutions += findSolution(j,end,begin,display);
			}
			data[y][x] = -1;
			//revert counted and data to original state.
			reset(counted,0);
			for(int i = 0; i < height; i++) for(int j = 0; j < width; j++){
				if(data[i][j] == -1) for(int k = 0; k < 8; k++){
					if(i+dy[k]>=0&&i+dy[k]<height&&j+dx[k]>=0&&j+dx[k]<width)
						counted[i+dy[k]][j+dx[k]] += 1;
				}
			}
		}
		return solutions;
	}

};


int width, height, mines;
bool solved = false;

class mineScreen{
	SDL_Surface *image[11];
	tagINPUT inp[10000];
	int inps;
	list <int> toFind;
public:
	void reset(solver *solve){
		//resets the screen: it pushes everything onto toFInd list, and yeah.
		toFind.clear();
		for(int x = 0; x < solve->width; x++){
			for(int y = 0; y < solve->height; y++){
				toFind.push_back((y<<10)|x);
				solve->data[y][x] = -1;
				solve->prevData[y][x] = -1;
			}
		}
		cout<<toFind.size()<<" "<<width<<" "<<height<<"\n";
	}
	void wait(int time){
		SDL_Delay(time);
	}

	/*void performClick(DWORD message1, DWORD message2){
		tagINPUT inp[1];
		memset(inp,0,sizeof(inp));
		inp[0].mi.time = 0;
		inp[0].mi.dy = inp[0].mi.dx = 0ll;
		inp[0].type = INPUT_MOUSE;
		inp[0].mi.dx = desiredX; inp[0].mi.dy = desiredY;
	//	inp[0].mi.dwFlags = MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
	//	SendInput(1,inp,sizeof(inp[0]));
	//	cout<<(message1==MOUSEEVENTF_LEFTDOWN?"Left":"Right")<<"\n";
		wait(40);
		inp[0].mi.dwFlags = message1|MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
		SendInput(1,inp,sizeof(inp[0]));
		inp[0].mi.dwFlags = message2|MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
		SendInput(1,inp,sizeof(inp[0]));
	}*///NOPE. 19/10/12 an array of inputs will be used, intead of this risky implimentation above.
	
	void performClick(DWORD message1, DWORD message2){
		inp[inps].mi.time = 0;
		inp[inps].mi.dy = inp[inps].mi.dx = 0ll;
		inp[inps].type = INPUT_MOUSE;
		inp[inps].mi.dx = desiredX; inp[inps].mi.dy = desiredY;
		inp[inps].mi.dwFlags = message1|MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
		inps++;
		send();
		inp[inps].mi.time = 0;
		inp[inps].mi.dy = inp[inps].mi.dx = 0ll;
		inp[inps].type = INPUT_MOUSE;
		inp[inps].mi.dx = desiredX; inp[inps].mi.dy = desiredY;
		inp[inps].mi.dwFlags = message2|MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
		inps++;
		send();
	}
	void send(){
		SendInput(inps,inp,sizeof(inp[0]));
		inps = 0;
		wait(40);
	}

private:
	int desiredX, desiredY;
	int nScreenWidth, nScreenHeight;
public:
	void moveCursor(int x, int y){
		//use windows input.
		desiredX = 65535*x/nScreenWidth; desiredY = 65535*y/nScreenHeight;
	};
	void focus(){
		moveCursor(x1-10,y1-10);
		performClick(MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_LEFTUP);
		wait(50);	
	}
	void leave(){
		focus();
		moveCursor(x2+200,y1+50);
		performClick(MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_LEFTUP);
		wait(50);
	}
	void click(int x, int y, solver* solve){
		int w = width/solve->width, h = height/solve->height;
		moveCursor(x1+x*width/solve->width+w/2,y1+y*height/solve->height+h/2);
		performClick(MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_LEFTUP);
		toFind.push_back((y<<10)|x);
	}
	void flag(int x, int y, solver* solve){
		int w = width/solve->width, h = height/solve->height;
		moveCursor(x1+x*width/solve->width+w/2,y1+y*height/solve->height+h/2);
		performClick(MOUSEEVENTF_RIGHTDOWN,MOUSEEVENTF_RIGHTUP);
		toFind.push_back((y<<10)|x);
	}
	unsigned char surface[1400][1080][3];

	void CaptureScreen(unsigned char dest[1400][1080][3] = NULL){
		if(dest==NULL) dest = surface;
		nScreenWidth = GetSystemMetrics(SM_CXSCREEN);
		nScreenHeight = GetSystemMetrics(SM_CYSCREEN);
		HWND hDesktopWnd = GetDesktopWindow();
		HDC hDesktopDC = GetDC(hDesktopWnd);
		HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);
		HBITMAP hCaptureBitmap = CreateCompatibleBitmap(hDesktopDC, nScreenWidth, nScreenHeight);
		SelectObject(hCaptureDC, hCaptureBitmap); 

		BitBlt(hCaptureDC, 0, 0, nScreenWidth, nScreenHeight, hDesktopDC, 0,0, SRCCOPY|CAPTUREBLT); 

		BITMAPINFO bmi = {0}; 
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader); 
		bmi.bmiHeader.biWidth = nScreenWidth; 
		bmi.bmiHeader.biHeight = nScreenHeight; 
		bmi.bmiHeader.biPlanes = 1; 
		bmi.bmiHeader.biBitCount = 32; 
		bmi.bmiHeader.biCompression = BI_RGB; 

		RGBQUAD *pPixels = new RGBQUAD[nScreenWidth * nScreenHeight]; 

		GetDIBits(
			hCaptureDC, 
			hCaptureBitmap, 
			0,  
			nScreenHeight,  
			pPixels, 
			&bmi,  
			DIB_RGB_COLORS
		);  

		// write:
		int p;
		int x, y;

		for(y = y1; y < y2; y++){
			for(x = x1; x < x2; x++){
				p = (nScreenHeight-y-1)*nScreenWidth+x; // upside down
				unsigned char r = pPixels[p].rgbRed;
				unsigned char g = pPixels[p].rgbGreen;
				unsigned char b = pPixels[p].rgbBlue;
				dest[y-y1][x-x1][0] = r; dest[y-y1][x-x1][1] = g; dest[y-y1][x-x1][2] = b;
			}
		}
		delete pPixels;
	}
	//responsible for getting mines off the screen and into the system.
	int x1, y1, x2, y2;
	int width, height;
	mineScreen(string directory){
		inps = 0;
		POINT p;
		cout<<"Hover at exactly the top left corner of the minefield, then hit enter.";
		system("Pause");
		GetCursorPos(&p);
		x1 = p.x,y1 = p.y;
		cout<<"Hover at exactly the bottom right corner of the minefield, then hit enter.";
		system("pause");
		GetCursorPos(&p);
		x2 = p.x,y2 = p.y;

		width = x2-x1; height = y2-y1;
		//read all the images.
		//used to display the average r,g and b of the pictures.
		string names[11] = {"-2","-1","0","1","2","3","4","5","6","7","8"};

		for(int i = 0; i < 11; i++) names[i] = directory+names[i]+".bmp";
		for(int i = -2; i <= 8; i++){
			image[i+2]= SDL_LoadBMP(names[i+2].c_str());
		}
		inps = 0;
	}
	//identifying is harder than I thought. I will need to write what SSD calls "custom logic" ಠ_ಠ sould just be called competent programming. New function, AIO 2012 Q3 style.

	int getError(SDL_Surface *image, unsigned char data[1400][1080][3], int x1,int y1, int w, int h){
		int error = 0;
		//put both on two parallel arrays.
		int r[3000],g[3000],b[3000];
		int rs=0,bs=0,gs=0;

		int iw = image->w, ih = image->h;
		for(int x = w/6; x < w; x++){
			for(int y = h/6; y < h; y++){
				int r1 = (int)data[y1+y][x1+x][0], g1 = (int)data[y1+y][x1+x][1], b1 = (int)surface[y1+y][x1+x][2];
				int p2 = getpixel(image,iw*x/w,ih*y/h);
				int b2 = p2&255, g2 = (p2>>8)&255, r2 = (p2>>16)&255;
				r[rs++]=(r2-r1); g[gs++]=(g2-g1); b[bs++]=(b2-b1);
			}
		}

		//use AIO Q3 to solve this minimum question.
		sort(r,r+rs);sort(g,g+gs);sort(b,b+bs);
		for(int i = 0; i < rs; i++){
			//assume r,g and b are equal in size
			error += abs(r[i]-r[rs>>1]);
			error += abs(g[i]-g[rs>>1]);
			error += abs(b[i]-b[rs>>1]);
		}
		error += abs(g[gs>>1]-r[rs>>1])*gs/3;
		error += abs(r[gs>>1]-b[rs>>1])*gs/3;
		error += abs(g[gs>>1]-b[rs>>1])*gs/3;
		return error;
	}

	void identify(solver *solve){
		//will try to identify all the squares, then display them. Still in testing stages.
		/*//loop all squares
		for(int i = 0; i < solve->width; i++){
			for(int j = 0; j < solve->height; j++){
				if(solve->data[j][i] != -1) continue;
				//try to solve this.
				int x1, y1, w = width/solve->width, h = height/solve->height;	//these ones are local.
				x1 = width*i/solve->width;
				y1 = height*j/solve->height;
				//will calculate pixel for pixel difference.
				pair <int,int> best = make_pair((1<<30),-3);
				for(int a = -2; a <= 8; a++){
					//test for image a+2
					int error = getError(image[a+2],surface,x1,y1,w,h);
					best = min(best,make_pair(error,a));
				}
				//it is now determined that best.second is the most appropriate value for solve's data at position j,i.
				solve->data[j][i] = best.second;
			}
		}*/
		//nope. Loop thorught list.
		for(list<int>::iterator i = toFind.begin(); i != toFind.end(); i++){
			int x = (*i)&1023, y = ((*i)>>10)&1023;
			if(solve->data[y][x] != -1) continue;
			//try to solve this.
			int x1, y1, w = width/solve->width, h = height/solve->height;	//these ones are local.
			x1 = width*x/solve->width;
			y1 = height*y/solve->height;
			//will calculate pixel for pixel difference.
			pair <int,int> best = make_pair((1<<30),-3);
			for(int a = -2; a <= 8; a++){
				//test for image a+2
				int error = getError(image[a+2],surface,x1,y1,w,h);
				best = min(best,make_pair(error,a));
			}
			//it is now determined that best.second is the most appropriate value for solve's data at position j,i.
			solve->data[y][x] = best.second;
			if(best.second == 0){//keep finding
				int dx[8] = {-1,0,1,1,1,0,-1,-1}, dy[8] = {-1,-1,-1,0,1,1,1,0};
				for(int k = 0; k < 8; k++){
					if(y+dy[k]<0||y+dy[k]>=solve->height||x+dx[k]<0||x+dx[k]>=solve->width) continue;
					toFind.push_back(((y+dy[k])<<10)|(x+dx[k]));
				}
			}
		}
		toFind.clear();
	}
	void render(SDL_Surface *screen){
		//solely for testing. Draws the contents of my surface onto screen.
		for(int i = 0; i < width; i++){
			for(int j =0; j < height; j++){
				SDL_Rect rect = {i,j,1,1};
				int r = surface[j][i][0], g = surface[j][i][1], b = surface[j][i][2];
				SDL_FillRect(screen,&rect,(r<<16)|(g<<8)|(b));
			}
		}
		for(int i = -2; i <= 8; i++){
			SDL_Rect rect = {i*60,height+10,60,60};
			SDL_BlitSurface(image[i+2],NULL,screen,&rect);
		}
	}
	void renderSquare(SDL_Surface *screen, solver *solve){
		//yep, renders a user inputted square.
		int x, y;
		cout<<"Render: ";
		cin>>x>>y;
		int x1, y1, w = width/solve->width, h = height/solve->height;	//these ones are local.
		x1 = width*x/solve->width;
		y1 = height*y/solve->height;
		for(int x = 0; x < w; x++){
			for(int y = 0; y < h; y++){
				int r1 = (int)surface[y1+y][x1+x][0], g1 = (int)surface[y1+y][x1+x][1], b1 = (int)surface[y1+y][x1+x][2];
				SDL_Rect rect = {x,y,1,1};
				SDL_FillRect(screen,&rect,(r1<<16)|(g1<<8)|b1);
			}
		}
	}
};

void pictest(){
	//used to display the average r,g and b of the pictures.
	string names[11] = {"-2","-1","0","1","2","3","4","5","6","7","8"};

	char filename[100];
	_getcwd(filename,100);
	string f(filename);
	for(int i = 0; i < 11; i++) names[i] = "Pictures\\"+names[i]+".bmp";
	for(int i = -2; i <= 8; i++){
		cout<<i<<" "<<names[i+2];
		SDL_Surface *image= SDL_LoadBMP(names[i+2].c_str());
		
		//get average r, g and b
		int r=0,b=0,g=0,t=0;
		for(int x = 0; x < image->w; x++){
			for(int y = 0; y < image->h; y++){
				t++;
				int p = getpixel(image,x,y);
				r+=(p>>16)&255; g += (p>>8)&255; b += (p)&255;
			}
		}

		r/=t;g/=t;b/=t;
		cout<<names[i+2]<<": \tr= "<<r<<", g= "<<g<<", b= "<<b<<"\n";
	}
}

int main(){
	//pictest();
	cout<<"Width, height and #mines: ";
	cin>>width>>height>>mines;
	solver solve(width,height,mines);
	SDL_Init(SDL_INIT_EVERYTHING);

	mineScreen ms("Pictures\\");
	
	/*image get.
	ms.CaptureScreen();
	SDL_Surface *s = SDL_SetVideoMode(500,500,32,SDL_SWSURFACE|SDL_DOUBLEBUF);
	
	while(true){
		SDL_FillRect(s,NULL,200);
	ms.renderSquare(s,&solve);
	SDL_Flip(s);
	}
	
	//*/
	
	//begin playing loop.
	while(true){
		cout<<"Ready?\n";system("pause");
		for(int i =0 ; i < 16; i++) for(int j = 0; j <30 ; j++) solve.data[i][j] = -1;
		solved = false;
		ms.reset(&solve);
	while(!solved){
		ms.CaptureScreen();
		ms.identify(&solve);
		solve.display();
		list <result> results = solve.solve(false,true);
		bool anything = false;
		//display results
		int ii =0;

		vector <pair<int,pair<double,bool>> > proc;
		for(list<result>::iterator i = results.begin(); i!= results.end(); i++){
			ii++;
			bool shouldInfer = false;
			int dx[8] = {-1,0,1,1,1,0,-1,-1}, dy[8] = {-1,-1,-1,0,1,1,1,0};
			for(int k = 0; k < 8; k++){
				if(i->x+dx[k] <0 || i->x+dx[k] >= width || i->y+dy[k] < 0 || i->y+dy[k] >= height) continue;
				if(solve.data[i->y+dy[k]][i->x+dx[k]] >=0) shouldInfer = true;
			}
			if(!shouldInfer){
				if(!anything){
					//cout<<proc.size()+1<<": Any other: "<<(i->isMine?"Mine":"Not mine")<<"\t"<<i->uncertainty<<"\n";
					proc.push_back(make_pair((i->x<<10)|i->y,make_pair(i->uncertainty,i->isMine)));
					anything = true;
				}
			}else{
				//cout<<proc.size()+1<<": "<<i->x<<", "<<i->y<<" "<<(i->isMine?"Mine":"Not mine")<<"\t"<<i->uncertainty<<"\n";
				proc.push_back(make_pair((i->x<<10)|i->y,make_pair(i->uncertainty,i->isMine)));
			}
		}
		//cout<<"Select the facts (fact X to fact Y) you want to automate. (X Y)\n";
		cout<<"Number of guesses made: "<<solve.guesses<<"\n";
		cout<<"Luck remaining: "<<solve.luckiness<<"\n";
		cout<<"|--------------------|\n-";
		for(int i = 0; i < solve.luckiness*20; i++) cout<<".";
		cout<<"\n|--------------------|\n";
		cout<<"\n";
		int x, y; ii = 0;
		/*
		cin>>x>>y;
		//impliment.
		if(x<=y){
			ms.focus();	//bring minesweeper window to focus.
		}*/	//make automagic.
		ms.focus();
		bool made = false;
		for(int i = 0; i < proc.size(); i++){
			if(proc[i].second.first != 0.0){
				if(made) continue;
				if(proc[i].second.second) continue;
				solve.luckiness *= (1.0-proc[i].second.first);
				solve.guesses++;
			}
			made = true;
			if(proc[i].second.second){
				//is mine. flag.
				ms.flag((proc[i].first>>10),proc[i].first&1023,&solve);
			}else{
				//is not mine. Click.
				ms.click((proc[i].first>>10),proc[i].first&1023,&solve);
			}
		}
		ms.send();
		ms.wait(40);
		if(proc.size()==0){
			solved = true;
		}
		ms.leave();
	}
	}
}
