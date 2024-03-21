```
 __  __                _     _               ____                                 
|  \/  | __ _ _ __ ___| |__ (_)_ __   __ _  / ___|  __ _ _   _  __ _ _ __ ___ ___ 
| |\/| |/ _` | '__/ __| '_ \| | '_ \ / _` | \___ \ / _` | | | |/ _` | '__/ _ / __|
| |  | | (_| | | | (__| | | | | | | | (_| |  ___) | (_| | |_| | (_| | | |  __\__ \
|_|  |_|\__,_|_|  \___|_| |_|_|_| |_|\__, | |____/ \__, |\__,_|\__,_|_|  \___|___/
                                        |___/            |_|                         
```
---
Structure && Implementation
---
The code has a very simple structure similar to the sequential approach.

Whilst the sequential approach has steps 0 through 4 - 5 if you include freeing the resources - in
the main function, in order to compute the image processing in a parallel fashion I had to make a
few slight changes.

First of, I decided to move the memory allocation for all needed variables up to the beginning of
the main function. This part as well as the initialization of the contour map (step 0) and reading
the image can't be parallelized, so I chose to execute them atomically in the main function.

Next I created all the possible threads (using the P parameter of the main function) and using
these threads, I run a function ('thread_function') which executes steps 1 through 5. All needed
variables are passed to the function as a struct. The struct contains the name of the output
file, the thread ID, the total number of threads, the original image, a pointer to the address
where the scaled image and the grid will be stored, the contour map, and a pointer to the barrier
for thread synchronization.

For the actual parallelization I simply split all the iterations, be it a simple for loop or a
nested one - corresponding to iteration over the whole image or the grid - into "slices" using
start and end indices, which are calculated based on the thread ID. Before I moved onto the next
step I had to make sure all threads were done processing their respective "slice" in order to
guarantee the correctness of the algorithm. This thread synchronization was done using a barrier.
And after applying the same logic to steps 1 through 3 I only had to write the image to the output
file atomically (so that the threads don't overwrite each other) and free the resources.

---
Feedback && Comments
---
This was a very good introduction to the world of parallel programming. I very quickly realized
not only the purpose, being able to see the huge speedup, but also some basic methods of achieving
faster code though parallelization. I had a lot of fun working on this project and I hope to be
able to work on more projects like this in the future.
