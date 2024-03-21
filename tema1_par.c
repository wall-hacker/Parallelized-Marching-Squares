#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define CONTOUR_CONFIG_COUNT    16
#define FILENAME_MAX_SIZE       50
#define STEP                    8
#define SIGMA                   200
#define RESCALE_X               2048
#define RESCALE_Y               2048

#define CLAMP(v, min, max) if(v < min) { v = min; } else if(v > max) { v = max; }
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// Creates a map between the binary configuration (e.g. 0110_2) and the corresponding pixels
// that need to be set on the output image. An array is used for this map since the keys are
// binary numbers in 0-15. Contour images are located in the './contours' directory.
ppm_image **init_contour_map() {
    ppm_image **map = (ppm_image **)malloc(CONTOUR_CONFIG_COUNT * sizeof(ppm_image *));
    if (!map) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    for (int i = 0; i < CONTOUR_CONFIG_COUNT; i++) {
        char filename[FILENAME_MAX_SIZE];
        sprintf(filename, "./contours/%d.ppm", i);
        map[i] = read_ppm(filename);
    }

    return map;
}

// Updates a particular section of an image with the corresponding contour pixels.
// Used to create the complete contour image.
void update_image(ppm_image *image, ppm_image *contour, int x, int y) {
    for (int i = 0; i < contour->x; i++) {
        for (int j = 0; j < contour->y; j++) {
            int contour_pixel_index = contour->x * i + j;
            int image_pixel_index = (x + i) * image->y + y + j;

            image->data[image_pixel_index].red = contour->data[contour_pixel_index].red;
            image->data[image_pixel_index].green = contour->data[contour_pixel_index].green;
            image->data[image_pixel_index].blue = contour->data[contour_pixel_index].blue;
        }
    }
}

// Calls `free` method on the utilized resources.
void free_resources(ppm_image *image, ppm_image **contour_map, unsigned char **grid, int step_x) {
    for (int i = 0; i < CONTOUR_CONFIG_COUNT; i++) {
        free(contour_map[i]->data);
        free(contour_map[i]);
    }
    free(contour_map);

    for (int i = 0; i <= image->x / step_x; i++) {
        free(grid[i]);
    }
    free(grid);

    free(image->data);
    free(image);
}

// Thread data structure
struct thread_data {
    const char* output_file;
    int thread_id;
    int num_threads;
    int step_x;
    int step_y;
    int p;
    int q;
    ppm_image *image;
    ppm_image *scaled_image;
    unsigned char **grid;
    ppm_image **contour_map;
    pthread_barrier_t *barrier;
};

// The thread function contains the parallelized adaptaion of steps 1-4 from the sequential implementation
void *thread_function(void* arg) {
    struct thread_data *data = (struct thread_data *)arg;

    // 1. Rescale image
    uint8_t sample[3];
    if (data->image->x > RESCALE_X || data->image->y > RESCALE_Y) {
        // calculate indeces for thread start and end
        int start_rescale = data->thread_id * (double)data->scaled_image->x / data->num_threads;
        int end_rescale = MIN((data->thread_id + 1) * (double)data->scaled_image->x / data->num_threads, data->scaled_image->x);

        // use bicubic interpolation for scaling
        for (int i = start_rescale; i < end_rescale; i++) {
            for (int j = 0; j < data->scaled_image->y; j++) {
                float u = (float)i / (float)(data->scaled_image->x - 1);
                float v = (float)j / (float)(data->scaled_image->y - 1);
                sample_bicubic(data->image, u, v, sample);

                data->scaled_image->data[i * data->scaled_image->y + j].red = sample[0];
                data->scaled_image->data[i * data->scaled_image->y + j].green = sample[1];
                data->scaled_image->data[i * data->scaled_image->y + j].blue = sample[2];
            }
        }
    } else {
        if (data->thread_id == 0) {
            // Only one thread should copy the image
            memcpy(data->scaled_image->data, data->image->data, data->image->x * data->image->y * sizeof(ppm_pixel));
        }
    }

    // Wait for all threads to finish
    pthread_barrier_wait(data->barrier);

    // 2. Sample the grid
    // calculate indeces for thread start and end
    int start_p = data->thread_id * (double)data->p / data->num_threads;
    int end_p = MIN((data->thread_id + 1) * (double)data->p / data->num_threads, data->p);
    int start_q = data->thread_id * (double)data->q / data->num_threads;
    int end_q = MIN((data->thread_id + 1) * (double)data->q / data->num_threads, data->q);

    for (int i = start_p; i < end_p; i++) {
        for (int j = 0; j < data->q; j++) {
            ppm_pixel curr_pixel = data->scaled_image->data[i * data->step_x * data->scaled_image->y + j * data->step_y];

            unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

            if (curr_color > SIGMA) {
                data->grid[i][j] = 0;
            } else {
                data->grid[i][j] = 1;
            }
        }
    }

    for (int i = start_p; i < end_p; i++) {
        ppm_pixel curr_pixel = data->scaled_image->data[i * data->step_x * data->scaled_image->y + data->scaled_image->x - 1];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA) {
            data->grid[i][data->q] = 0;
        } else {
            data->grid[i][data->q] = 1;
        }
    }
    for (int j = start_q; j < end_q; j++) {
        ppm_pixel curr_pixel = data->scaled_image->data[(data->scaled_image->x - 1) * data->scaled_image->y + j * data->step_y];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA) {
            data->grid[data->p][j] = 0;
        } else {
            data->grid[data->p][j] = 1;
        }
    }

    // Wait for all threads to finish
    pthread_barrier_wait(data->barrier);

    // 3. March the squares
    for (int i = start_p; i < end_p; i++) {
        for (int j = 0; j < data->q; j++) {
            unsigned char k = 8 * data->grid[i][j] + 4 * data->grid[i][j + 1] + 2 * data->grid[i + 1][j + 1] + 1 * data->grid[i + 1][j];
            update_image(data->scaled_image, data->contour_map[k], i * data->step_x, j * data->step_y);
        }
    }

    // Wait for all threads to finish
    pthread_barrier_wait(data->barrier);

    // 4. Write output
    if (data->thread_id == 0) {
        // Only one thread should write the image
        write_ppm(data->scaled_image, data->output_file);
    }

    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: ./tema1 <in_file> <out_file> <P>\n");
        return 1;
    }

    int num_threads = atoi(argv[3]);
    pthread_t threads[num_threads];
    struct thread_data thread_data_array[num_threads];
    
    // Barrier used to synchronize threads
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, num_threads);

    ppm_image *image = read_ppm(argv[1]);
    int step_x = STEP;
    int step_y = STEP;

    // 0. Initialize contour map
    ppm_image **contour_map = init_contour_map();

    // Allocate memory for scaled image and grid
    ppm_image *scaled_image = (ppm_image *)malloc(sizeof(ppm_image));
    scaled_image->x = RESCALE_X;
    scaled_image->y = RESCALE_Y;
    scaled_image->data = (ppm_pixel*)malloc(RESCALE_X * RESCALE_Y * sizeof(ppm_pixel));
    int p = scaled_image->x / step_x;
    int q = scaled_image->y / step_y;
    unsigned char **grid = (unsigned char **)malloc((p + 1) * sizeof(unsigned char*));
    for (int i = 0; i <= p; i++) {
        grid[i] = (unsigned char *)malloc((q + 1) * sizeof(unsigned char));
    }

    for (int i = 0; i < num_threads; i++) {
        thread_data_array[i].output_file = argv[2];
        thread_data_array[i].thread_id = i;
        thread_data_array[i].num_threads = num_threads;
        thread_data_array[i].step_x = step_x;
        thread_data_array[i].step_y = step_y;
        thread_data_array[i].p = p;
        thread_data_array[i].q = q;
        thread_data_array[i].image = image;
        thread_data_array[i].scaled_image = scaled_image;
        thread_data_array[i].grid = grid;
        thread_data_array[i].contour_map = contour_map;
        thread_data_array[i].barrier = &barrier;

        pthread_create(&threads[i], NULL, thread_function, (void *)&thread_data_array[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free_resources(scaled_image, contour_map, grid, step_x);
    free(image->data);
    free(image);

    return 0;
}
