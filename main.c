/**
 * based on http://zarb.org/~gc/html/libpng.html
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include <png.h>

#define USAGE 	fprintf (stdout, "Usage: ./stegpng [-e -d] <filename> [-m] <message>  [-o] new_filename"); exit(EXIT_FAILURE)

#define FREE_ALL	if(!filename)free(filename);\
					if(!message)free(message);\
					if(!dst)free(dst)

#define DESTROY_READ_PNG(obj, info) 	(png_destroy_read_struct(&obj, info, NULL))
#define DESTROY_WRITE_PNG(obj, info) 	(png_destroy_write_struct(&obj, info))
#define DESTROY_ROW_POINTERS(obj) 		for (int y = 0; y < obj->height; y++){\
											free (obj->row_pointers[y]); \
										}\
										free (obj->row_pointers);\

#define DESTROY_PNG_DATA(obj) 			DESTROY_ROW_POINTERS(obj);\
										free (obj);\

#define CLEAR_BIT(obj, x) 		(obj&=~(1<<x))
#define SET_BIT(obj, x) 		(obj|=1<<x)
#define GET_BIT(obj, x) 		((obj>>x)&1)


typedef struct png_d png_data_t;
struct png_d {
	int width;
	int	height;
	png_byte color_type;
	png_byte bit_depth;

	png_bytep * row_pointers;
};

void
fatal (char * filename, png_data_t * data, bool d_data) {
	fprintf (stderr, "[ERROR] %d: %s - %s", errno, filename, strerror (errno));
	if (!d_data) {
		DESTROY_PNG_DATA (data);
	}
	exit (EXIT_FAILURE);
}

png_data_t *
read_png_file(char *filename) {
	int width;
	int height;
	png_byte color_type;
	png_byte bit_depth;
	png_bytep * row_pointers;

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
	  perror ("opening the file");
	  exit (EXIT_FAILURE);
  }
  png_data_t * data;

  png_structp png_p = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if(!png_p) 
	  return NULL;

  png_infop info = png_create_info_struct(png_p);
  if(!info) {
	  DESTROY_READ_PNG (png_p, NULL);
	  fclose (fp);
	  return NULL;
  }

  if(setjmp (png_jmpbuf (png_p) ) ) {
	  DESTROY_READ_PNG (png_p, &info);
	  fclose (fp);
	  return NULL;
  }

  png_init_io(png_p, fp);

  png_read_info(png_p, info);

  width      = png_get_image_width(png_p, info);
  height     = png_get_image_height(png_p, info);
  color_type = png_get_color_type(png_p, info);
  bit_depth  = png_get_bit_depth(png_p, info);

  // Read any color_type into 8bit depth, RGBA format.
  // See http://www.libpng.org/pub/png/libpng-manual.txt

  if(bit_depth == 16)
    png_set_strip_16(png_p);

  if(color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png_p);

  // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
  if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png_p);

  if(png_get_valid(png_p, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png_p);

  // These color_type don't have an alpha channel then fill it with 0xff.
  if(color_type == PNG_COLOR_TYPE_RGB ||
     color_type == PNG_COLOR_TYPE_GRAY ||
     color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_filler(png_p, 0xFF, PNG_FILLER_AFTER);

  if(color_type == PNG_COLOR_TYPE_GRAY ||
     color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png_p);

  png_read_update_info(png_p, info);

  row_pointers = (png_bytep *) malloc (sizeof(png_bytep) * height);
  for(int y = 0; y < height; y++) {
  	int row = png_get_rowbytes (png_p, info);
	//printf ("%d\n\n", row);
    row_pointers[y] = (png_byte *) malloc (row);
  }

  png_read_image(png_p, row_pointers);


  data = (png_data_t *) calloc (1, sizeof (png_data_t));
  data->height = height;
  data->width = width;
  data->color_type = color_type;
  data->bit_depth = bit_depth;
  data->row_pointers = row_pointers;

  fclose(fp);

  return data;
}

bool
write_png_file (char * filename, png_data_t * data) {
	int y;

	FILE *fp = fopen (filename, "wb");
	if (!fp) {
		
		free (data);
		return false;
	}

	png_structp png_p = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_p) {
		DESTROY_PNG_DATA (data);
		fclose (fp);
		return false;
	}

	png_infop info = png_create_info_struct (png_p);
	if (!info) {
		DESTROY_WRITE_PNG (png_p, NULL);
		DESTROY_PNG_DATA (data);
		fclose (fp);
		return false;
	}

	if (setjmp (png_jmpbuf (png_p))) {
		DESTROY_WRITE_PNG (png_p, &info);
		fclose (fp);
		return false;
	}

	png_init_io (png_p, fp);

	// Output is 8bit depth, RGBA format.
	png_set_IHDR (png_p,
				 info,
				 data->width, data->height,
				 data->bit_depth,
				 PNG_COLOR_TYPE_RGBA,
				 PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT,
				 PNG_FILTER_TYPE_DEFAULT
	);
	png_write_info (png_p, info);

	png_write_image (png_p, data->row_pointers);
	png_write_end (png_p, NULL);

	DESTROY_PNG_DATA (data);
	fclose (fp);
	return true;
}

void 
process_png_file (png_data_t * data, char * message) {
	int k = 0;
	int y = 0;
	for (int y = 0; y < data->height; y++) {
		png_bytep row = data->row_pointers[y];
		for (int x = 0; x < data->width*4; x=x+4) {
			for (int w = 0; w < 3; w++) {
				if (GET_BIT (message [y], k) && !GET_BIT (row [x+w], 0)) {
					SET_BIT (row [x+w], 0);
					
				} else if (!GET_BIT (message [y], k) && GET_BIT (row [x+w], 0)) {
					CLEAR_BIT (row [x+w], 0);
				}

				k++;
				if (k == 8) {
					k = 0;
					y++;
					if (y == strlen (message))
						return;
		
				}
				
			}
		}
	}
}

void 
get_message (png_data_t * data) {
	char message [1024];
	int k = 0;
	int y = 0;

	memset (message, 0, 1024);

	for (int y = 0; y < data->height; y++) {
		png_bytep row = data->row_pointers[y];
		for (int x = 0; x < data->width*4; x=x+4) {
			for (int w = 0; w < 3; w++) {
				if (GET_BIT (row [x+w], 0)) {
					SET_BIT (message [y], k);
					
				} else if (!GET_BIT (row [x+w], 0)) {
					CLEAR_BIT (message [y], k);
				}

				k++;
				if (k == 8) {
					y++;
					k = 0;
					if (message[y-1] == '\t' || y == 1024){
						message[y-1] = 0x00;
						printf ("%s\n", message);
						return;
					}
				}
				
			}
			
		}
	}
}

int main (int argc, char ** argv) {
	

	png_data_t * png_data;

	char * filename = NULL;
	char * dst = NULL;
	char * message = NULL;
	bool d = false;
	int c;

	if (argc < 3) {
		USAGE;
	} 


	while ((c = getopt (argc, argv, "e:d:m:o:h")) != -1) {
		switch (c) {
			case 'e': {
				filename = strdup (optarg);
				break;
			} case 'd': {
				d = true;
				filename =	strdup (optarg); 
				break;
			} case 'm': {
				printf ("%lu\n\n", strlen (optarg));
				message = (char *) calloc (1, strlen (optarg) + 2);
				sprintf (message, "%s\t\0", optarg);
				printf ("%lu\n\n", strlen (message));
				break;
			} case 'o': {
				dst = strdup (optarg); 
				break;
			} case 'h': {
				USAGE;
				break;
			} default: {
				USAGE;
				break;
			}
		}
	}

	if (!d) {
		if (!dst || !message) {
			FREE_ALL;
			USAGE;
		}
		if (!(png_data = read_png_file (filename))) {
			fprintf (stderr, "[ERROR] %d: %s - %s", errno, filename, strerror (errno));
			exit (EXIT_FAILURE);
		}
		process_png_file (png_data, message);
		free (message);
		if (!(write_png_file (dst, png_data))) {
			fprintf (stderr, "[ERROR] %d: %s - %s", errno, filename, strerror (errno));
			DESTROY_PNG_DATA (png_data);
			FREE_ALL;
			exit (EXIT_FAILURE);
		}
	} else {
		if (!filename) {
			FREE_ALL;		
			USAGE;
		}
		if (!(png_data = read_png_file (filename))) {
			fatal (filename, png_data, false);
		}
		get_message (png_data);
	}

	FREE_ALL;
	return 0;
}
