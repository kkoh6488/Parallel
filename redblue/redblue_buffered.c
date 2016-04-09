#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>
#include "debuggrid.h"
#include <math.h>
#include <time.h>
#include "redblueprocedure.h"

void board_init(int** grid, int size);
int malloc2darray(int ***array, int x, int y);
void setemptycells(int **subgrid, int height, int width, int intcolor); 
void setemptybuffercells(int *buf, int size, int color);
int free2darray(int ***array);
void updatetoprow(int *toprow, int *tempbuffer,  int size);
int tilespastthreshold(int **grid, int height, int width, int maxtilecount, int tilesize, int numtiles);
int counttiles(int **localgrid, int height, int width, int toprowindex, int tilesize, int tilesperrow, int numtiles, int maxcells);

int main(char argc, char** argv)
{
	if ((argc + 0) != 5) {	
		printf("Required arguments missing");
		return -1;
	}

	MPI_Init(NULL, NULL);
	int rank, worldsize;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &worldsize);	

	int n 				= strtol(argv[1], NULL, 10);	// Grid size
	int t 				= strtol(argv[2], NULL, 10);	// Tile size
	float  c 			= atof(argv[3]);				// Terminating threshold
	int maxiters 		= strtol(argv[4], NULL, 10);	// Max iterations
	int curriter 		= 0;							// Current iteration
	int **grid;											// Master grid
	int numtoexceedc	= (int)(t * t * c + 1);			// Cells to exceed the threshold
	int numtiles		= (n / t) * (n / t);
	clock_t start, end;
	double elapsed;	
	
	malloc2darray(&grid, n, n);
	
	if (rank == 0) {
		printf("Initializing board of size %d with tile size %d, threshold %f and max iterations %d, num to exceed %d \n", n, t, c, maxiters, numtoexceedc);
		board_init(grid, n);		
	}
	
	start = clock();
	if (worldsize > 1 && t != n) {
		// Init the local 2D array for this process
		//int rowsperproc 		= n / worldsize;
		//int extrarowsforproc 	= n % worldsize;		
		int mynumrows;
		//int procswithextrarows 	= extrarowsforproc % worldsize;		
		int tilesperrow 		= n / t;
		int remaindertiles, usedworldsize;

		if (worldsize > tilesperrow) {
			remaindertiles = 0;
			usedworldsize = tilesperrow;
		}
		else {
			remaindertiles = tilesperrow % worldsize;
			usedworldsize = worldsize;
		}

		MPI_Comm activecomm;
		int color = rank / tilesperrow;
		MPI_Comm_split(MPI_COMM_WORLD, color, rank, &activecomm);
		int grank, gsize;
		MPI_Comm_rank(activecomm, &grank);
		MPI_Comm_size(activecomm, &gsize);

		if (rank >= usedworldsize) {										// Quit if the process isn't needed
			printf("Exiting rank, usedworldsize %d: %d\n", usedworldsize, rank);
			MPI_Finalize();
			exit(0);
		}
		//printf("rank and size are %d, %d\n", grank, gsize);

		//int remaindertiles		= tilesperrow % worldsize;			// How many tiles are left after an even distribution
		int tilesperproc		= tilesperrow / gsize;			// How many tiles if we split evenly?
		int procswithextratiles = remaindertiles % gsize;		// How many processes get extra tile rows	
		int rowsperproc			= tilesperproc * t;		// 

		// Local arrays
		int **localgrid;
		
		// Assign the rows for this process
		// Split up the rows evenly
		
		if (grank < procswithextratiles) {
			//mynumrows = rowsperproc + extrarowsforproc / procswithextrarows;
			mynumrows = (tilesperproc + remaindertiles / procswithextratiles) * t;
		}
		else {
			//mynumrows = rowsperproc;
			mynumrows = tilesperproc * t;
		}

		malloc2darray(&localgrid, mynumrows, n);
		if (grank == 0)
		{
			printf("Split stats: tilesperrow %d, tilesperproc %d, remaindertiles: %d, procswithextratiles: %d, rowsperproc: %d\n", tilesperrow, tilesperproc, remaindertiles, procswithextratiles, rowsperproc);
			printf("Master num rows %d \n", mynumrows);
			for (int x = 0; x < mynumrows; x++) {
				for (int y = 0; y < n; y++) {
					localgrid[x][y] = grid[x][y];
					//printf("%d ", localgrid[x][y]);		
				}
				//printf("\n");
			}			
				
			int dest = 1;
			int counter = 0;
			//printf("Procs with extra rows: %d \n", procswithextrarows);
			//Send rows from master to worker processes
			for (int x = mynumrows; x < n; x++) {
				MPI_Send(&grid[x][0], n, MPI_INT, dest, 0, activecomm);
				counter++;
				if (dest < procswithextratiles) {
					if (counter == mynumrows) {
						dest++;
						counter = 0;
					}
				} 
				else if (counter == rowsperproc) {
					dest++;
					counter = 0;
				}	
			}
		}
		else {
			for (int x = 0; x < mynumrows; x++) {
				MPI_Recv(&localgrid[x][0], n, MPI_INT, 0, 0, activecomm, MPI_STATUS_IGNORE); 
			}
			
			printf("Proc %d \n", grank);	
			for (int x = 0; x < mynumrows; x++)
			{
				for (int y = 0; y < n; y++)
				{
					printf("%d ", localgrid[x][y]);
				}
				printf("\n");
			}
			
		}
		int* tempbotbuffer =  (int*) malloc (n * sizeof (int));
		int* botbuffer =  (int*) malloc (n * sizeof (int)); 

		int src, dest;	
		while (curriter < maxiters) {	
			//printf("Starting iter %d on proc %d\n", curriter, rank);
			solveredturn(localgrid, mynumrows, n);
			setemptycells(localgrid, mynumrows, n,  1);
		
			// For each process, get the row number it needs for the bottom buffer
			// Each process sends its top row to the previous process bot row
			for (int i = 0; i < gsize; i++) {
				if (grank == i) {
					if (grank == 0) {
						dest = gsize - 1;
						src = grank + 1;
					}
					else if (grank == gsize - 1) {
						dest = grank - 1;
						src = 0;
					}
					else {
						dest = grank - 1;
						src = grank + 1; 
					}
					MPI_Sendrecv(&localgrid[0][0], n, MPI_INT, dest, 1, botbuffer, n, MPI_INT, src, 1, activecomm, MPI_STATUS_IGNORE);
				}					
			}	
			//printf("Received rows: proc %d \n", rank);			
			solveblueturn(localgrid, botbuffer, mynumrows, n);
			for (int i = 0; i < gsize; i++) {
				if (grank == i) {	
					// Flip the src and dest - send to the src and recv from the dest as we are sending the bot buffer back		
					MPI_Sendrecv(botbuffer, n, MPI_INT, src, 2, tempbotbuffer, n, MPI_INT, dest, 2, activecomm, MPI_STATUS_IGNORE);
					//printf("Proc %d received botbuffer:\n", rank);
					//print_array(tempbotbuffer, n);
				}
			}
			updatetoprow(&localgrid[0][0], tempbotbuffer,  n);
			setemptycells(localgrid, mynumrows, n, 2);
			setemptybuffercells(botbuffer, n, 2);
			
			// Now check if tiles exceed c. If not, proceed with the next iteration.
			int tileresult;
			int toprowindex = -1;				// The index in the whole grid of the top local row

			// Get the index of the top row
			if (grank >= procswithextratiles) {
				//printf("Proc %d > procswithextratiles %d\n", rank, procswithextratiles);
				toprowindex = (remaindertiles * t) + (grank * tilesperproc * t);
				//toprowindex = ((procswithextratiles + remaindertiles) * t) + (rank - remaindertiles) * t;
				//toprowindex = (procswithextratiles * (extrarowsforproc + rowspertile)) + (rank - procswithextrarows) * rowsperproc;
			}
			else {
				toprowindex = grank * procswithextratiles * (remaindertiles + t);
				//toprowindex = rank * procswithextrarows * extrarowsforproc;
			}

			printf("Proc %d with top row %d \n", grank, toprowindex);
			print_grid(localgrid, mynumrows, n);
			//void counttiles(int **localgrid, int *numred, int *numblue, int height, int width, int toprowindex, int tilesize, int tilesperrow)
			tileresult = counttiles(localgrid, mynumrows, n, toprowindex, t, tilesperrow, numtiles, numtoexceedc);
			
			/*MPI_Reduce(numred, temptilecountred, numtiles, MPI_INT, MPI_SUM, 0, activecomm);
			if (grank == 0) {
				for (int i = 0; i < numtiles; i++) {
					if (temptilecountred[i] >= numtoexceedc) {
						printf("Tile exceeded c: %d\n", i);
						tileresult = -1;
					}
				}
			}
			printf("Checked red");
			MPI_Reduce(numblue, temptilecountblue, numtiles, MPI_INT, MPI_SUM, 0, activecomm);
			if (grank == 0) {
				for (int i = 0; i < numtiles; i++) {
					if (temptilecountblue[i] >= numtoexceedc) {
						printf("Tile exceeded c: %d\n", i);
						tileresult = -1;
					}
				}
			}*/
			//MPI_Bcast(&tileresult, 1, MPI_INT, 0, activecomm);
			int allresult;
			MPI_Allreduce(&tileresult, &allresult, 1, MPI_INT, MPI_MIN, activecomm);
			if (allresult == -1) {
				break;
			}
			curriter++;
		}	
	}
	else
	{
		if (rank > 0) {
			MPI_Finalize();
			exit(0);
		}

		while (curriter < maxiters)
		{
			solveredturn(grid, n, n);
			setemptycells(grid, n, n, 1);
			solveblueturn(grid, NULL, n, n);
			setemptycells(grid, n, n, 2);
			if (tilespastthreshold(grid, n, n, numtoexceedc, t, numtiles) == 1)
			{
				break;
			}
			curriter++;	
		}	
	}
	end = clock();
	elapsed = (double)(end - start) / CLOCKS_PER_SEC;
	printf("Execution time: %d\n", elapsed);
	MPI_Finalize();	
}
		
// For each process, look at the top row and compare it with the bottom buffer of (rank - 1) process
// If number moved in (ie is 3) in the bottom buffer, mark the new entry in the top row of this process.
void updatetoprow(int *toprow, int *tempbuffer, int size) {
	for (int i = 0; i < size; i++)
	{
		if (tempbuffer[i] == 3)
		{
			toprow[i] = 2;
		}	
	}
}

int counttiles(int **localgrid, int height, int width, int toprowindex, int tilesize, int tilesperrow, int numtiles, int maxcells) {
	
	int* numred				= (int*) malloc (numtiles * sizeof(int));
	int* numblue			= (int*) malloc (numtiles * sizeof(int));
	// Zero out arrays - just in case to get rid of old values
	for (int i = 0; i < numtiles; i++) {
		numred[i] = 0;
		numblue[i] = 0;
	}
	int result = 0;
	for (int x = 0; x < height; x++) {
		int rowindex = toprowindex + x;
		//if (rowindex % tilesize == 0)
		//{
		for (int y = 0; y < width; y++) {
			int tilenum = (rowindex / tilesize) *  tilesperrow + (y / tilesize);
			printf("tilenum :%d, red: %d, blue: %d\n", tilenum, numred[tilenum], numblue[tilenum]);
			//printf("t[%d]", tilenum);
			if (localgrid[x][y] == 1) {
				numred[tilenum]++;
			}
			else if (localgrid[x][y] == 2) {
				numblue[tilenum]++;
			}
			//printf("x + 1, y + 1, tilesize: %d, %d, %d\n", x + 1, y + 1, tilesize);
			if ((x + 1) % tilesize == 0 && (y + 1) % tilesize == 0) {
				//printf("Checking tile %d\n", tilenum);
				if (numred[tilenum] >= maxcells || numblue[tilenum] >= maxcells) {
					printf("Tile %d exceeded max @ red:%d, blue:%d\n", tilenum, numred[tilenum], numblue[tilenum]);
					result = -1;
				} 
			}
		}
		//}
		//printf("\n");
	}
	return result;
}

// Check if a tiles in the given grid exceeds the threshold
int tilespastthreshold(int **grid, int height, int width, int maxtilecount, int tilesize, int numtiles)
{
	int tilenum = 0;				
	int* numred = (int *) malloc (numtiles * sizeof(int));
	int* numblue = (int *) malloc (numtiles * sizeof(int));
	int tilesperrow = width / tilesize;
	int result = 0;
	
	// Zero out arrays - just in case to get rid of old values
	for (int i = 0; i < numtiles; i++)
	{
		numred[i] = 0;
		numblue[i] = 0;
	}
	for (int x = 0; x < height; x++)
	{
		for (int y = 0; y < width; y++)
		{
			tilenum = (x / tilesize) *  tilesperrow + (y / tilesize);
			//printf("t[%d]", tilenum);
			if (grid[x][y] == 1)
			{
				numred[tilenum] += 1;
			}
			else if (grid[x][y] == 2)
			{
				numblue[tilenum] += 1;
			}
		}
		//printf("\n");
	}
	for (int i = 0; i < numtiles; i++)
	{
		//printf("Num in tile %d (max %d) - red: %d, blue %d \n", i, maxtilecount, numred[i], numblue[i]);
		if (numred[i] >= maxtilecount || numblue[i] >= maxtilecount)
		{
			printf("Exceeded at tile %d\n", i);
			result = 1;			
		}
	}
	return result;
}

// Takes the subgrid and changes any "moved" values (ie 3 and 4) and turns it into an empty cell (ie 0) or a cell of the given color respectively.
// Done at the end of each half-turn.
void setemptycells(int **subgrid, int height, int width, int intcolor)
{
	for (int x = 0; x < height; x++)
	{
		for (int y = 0; y < width; y++)
		{
			if (subgrid[x][y] == 4)
			{
				subgrid[x][y] = 0;
			}
			else if (subgrid[x][y] == 3)
			{
				subgrid[x][y] = intcolor;
			}	
		} 
	}
}

void setemptybuffercells(int *buf, int size, int color)
{
	for (int x = 0; x < size; x++)
	{
		if (buf[x] == 4)
		{
			buf[x] = 0;
		}
		else if (buf[x] == 3)
		{
			buf[x] = color;
		}
	}
}

int malloc2darray(int ***array, int x, int y)
{
	int *i = (int *) malloc (x * y * sizeof(int));
	if (!i)
	{
		return -1;
	}
	
	// Allocate the row pointers
	(*array) = malloc (x * sizeof(int*));
	if (!(*array))
	{
		free(i);
		return -1;
	}
	
	for (int a = 0; a < x; a++)
	{
		(*array)[a] = &(i[a * y]);
	}
	//printf("Init grid OK\n");
	return 0;
}

int free2darray(int ***array)
{
	free(&((*array)[0][0]));
	free(*array);
	return 0;
}

void  board_init(int** grid, int size)
{
	printf("Initializing\n");
	float max = 1.0;
	srand(time(NULL));			// Set the random number seed
	for (int x = 0; x < size; x++)
	{
		for (int y = 0; y < size; y++)
		{
			float val = ((float)rand()/(float)(RAND_MAX)) * max;
			if (val <= 0.33)
			{
				grid[x][y] = 0;
			}
			else if (val <= 0.66)
			{
				grid[x][y] = 1; 

			}
			else
			{
				grid[x][y] = 2;
			}
			printf("%d", grid[x][y]); 
		}
		printf("\n");							
	}			
}
