#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <jerror.h>
#include <openssl/sha.h>

// constants for the embed/extract mode
#define EMBED   1
#define EXTRACT 2
#define CHECK_BIT(var, pos) ((var) & (1LL << (pos)))

void main(int argc, char **argv)
{
    // determines whether twe are embedding or extracting
    int mode;

    // types for the libjpeg input object
    struct jpeg_decompress_struct cinfo_in;
    struct jpeg_error_mgr jpegerr_in;
    jpeg_component_info *component;
    jvirt_barray_ptr *DCT_blocks;

    // types for the libjpeg output objet
    struct jpeg_compress_struct cinfo_out;
    struct jpeg_error_mgr jpegerr_out;

    // file handles
    FILE *file_in;
    FILE *file_out;     // only used for embedding
    FILE *file_payload; // only used for embedding

    // to store the payload
    unsigned long payload_length; 
    unsigned char *payload_bytes;
    unsigned long BUFFSIZE=1024*1024; //1MB hardcoded max payload size, plenty

    // the key string, and its SHA-1 hash
    char *key;
    unsigned char *keyhash;

    // useful properties of the image
    unsigned long blocks_high, blocks_wide;

    // for the example code
    int block_y, block_x, u, v;

    // to store the bit stream
    unsigned char *bit_stream;
    unsigned char *stream_temp; 
    unsigned char *payload_bits;

    // to store the pseudoRandom ordering of DCT coefficients
    int *pseudoRand;

    // to store the extracted payload_length in bits
    unsigned char *extract_payload_length;

    // to store the extract_payload_bits
    unsigned char *extract_payload_bits;

    // to store extracted payload_bytes (convert extracted bits to bytes)
    
    // parse parameters
    if(argc==4 && strcmp(argv[1],"embed")==0)
    {
        mode=EMBED;
        key=argv[3];
    }
    else if(argc==3 && strcmp(argv[1],"extract")==0)
    {
        mode=EXTRACT;
        key=argv[2];
    }
    else
    {
        fprintf(stderr, "Usage: GaragePythons embed cover.jpg key <payload >stego.jpg\n");
        fprintf(stderr, "Or     GaragePythons extract key <stego.jpg\n");
        exit(1);
    }

    if(mode==EMBED)
    {
        // read ALL (up to eof, or max buffer size) of the payload into the buffer
        if((payload_bytes=malloc(BUFFSIZE))==NULL)
        {
            fprintf(stderr, "Memory allocation failed!\n");
            exit(1);
        }
        file_payload=stdin;
        payload_length=fread(payload_bytes, 1, BUFFSIZE, file_payload);
        fprintf(stderr, "Embedding payload of length %ld bytes...\n", payload_length);

        // TASK 1: convert payload into bit stream (or ternary alphabet if you prefer) and unambiguously encode its length

        // Malloc for bit stream
        // bit_stream = payload_bits + payload_length in bits

        if ((bit_stream = malloc((payload_length + sizeof(unsigned long)) * 8)) == NULL)
        {
            fprintf(stderr, "Malloc allocation failed for bit_stream\n");
            exit(1);
        }

        if ((stream_temp = malloc(payload_length * 8)) == NULL)
        {
            fprintf(stderr, "Malloc allocation failed for stream_temp\n"); 
            exit(1); 
        }

        fprintf(stderr, "PRINTING PAYLOAD_BYTES IN BIT STREAM: \n"); 
        // STREAM_TEMP: Payload_bytes into bits, stored as a temp in stream_temp
        int bitPlacement = 0; 
        for (int i = 0; i < payload_length; i++)
        {
            unsigned int c = payload_bytes[i];
            
            for (int j = 7; j >= 0; --j)
            {
                stream_temp[bitPlacement] = ((c & (1 << j)) ? 1 : 0);
        //        fprintf(stderr, "%d", stream_temp[bitPlacement]);
                bitPlacement++; 
            }
        }
       
        fprintf(stderr, "\n"); 
        // PAYLOAD_BITS: Takes the length of the payload and writes in binary
        
        fprintf(stderr, "Convert the payload length in bytes to bit array: \n"); 
        payload_bits = malloc(sizeof(unsigned long) * 8);

        unsigned long n = payload_length;
        for(int i = 0; i < sizeof(unsigned long) * 8; i++)
        {
            payload_bits[sizeof(unsigned long) * 8 - 1 - i ] = n % 2;
            n /= 2;
            fprintf(stderr, "%d", payload_bits[i]);   
        }
        fprintf(stderr, "\n"); 

        /*
        int position = sizeof(unsigned long) * 8;
        position = position - 1;
        int i = 0;

        while (position >= 0)
        {
            if (CHECK_BIT(payload_length, position))
            {
                payload_bits[i] = 1;
            }
            else
                payload_bits[i] = 0;
            --position;
            i++;
        }
        */

        memcpy(bit_stream, payload_bits, sizeof(unsigned long) * 8);
        memcpy(bit_stream + sizeof(unsigned long) * 8, stream_temp, payload_length * 8); 

        fprintf(stderr, "PRINTING BIT_STREAM: \n"); 

        for (int i = 0; i < (payload_length + sizeof(unsigned long)) * 8; i++)
        {
          //  fprintf(stderr, "%d", bit_stream[i]); 
        }
        fprintf(stderr, "\n"); 

        free(payload_bits);
        free(stream_temp); 
    }  

    // open the input file
    if(mode==EMBED)
    {
        if((file_in=fopen(argv[2],"rb"))==NULL)
        {
            fprintf(stderr, "Unable to open cover file %s\n", argv[2]);
            exit(1);
        }
    }
    else if(mode==EXTRACT)
    {
        file_in=stdin;
    }

    // libjpeg -- create decompression object for reading the input file, using the standard error handler
    cinfo_in.err = jpeg_std_error(&jpegerr_in);
    jpeg_create_decompress(&cinfo_in);

    // libjpeg -- feed the cover file handle to the libjpeg decompressor
    jpeg_stdio_src(&cinfo_in, file_in);

    // libjpeg -- read the compression parameters and first (luma) component information
    jpeg_read_header(&cinfo_in, TRUE);
    component=cinfo_in.comp_info;

    // these are very useful (they apply to luma component only)
    blocks_wide=component->width_in_blocks;
    blocks_high=component->height_in_blocks;
    // these might also be useful:
    // component->quant_table->quantval[i] gives you the quantization factor for code i (i=0..63, scanning the 8x8 modes in row order)

    // libjpeg -- read all the DCT coefficients into a memory structure (memory handling is done by the library)
    DCT_blocks=jpeg_read_coefficients(&cinfo_in);

    // if embedding, set up the output file
    // (we had to read the input first so that libjpeg can set up an output file with the exact same parameters)
    if(mode==EMBED)
    {
        // libjpeg -- create compression object with default error handler
        cinfo_out.err = jpeg_std_error(&jpegerr_out);
        jpeg_create_compress(&cinfo_out);

        // libjpeg -- copy all parameters from the input to output object
        jpeg_copy_critical_parameters(&cinfo_in, &cinfo_out);

        // libjpeg -- feed the stego file handle to the libjpeg compressor
        file_out=stdout;
        jpeg_stdio_dest(&cinfo_out, file_out);
    }


    // At this point the input has been read, and an output is ready (if embedding)
    // We can modify the DCT_blocks if we are embedding, or just print the payload if extracting

    if((keyhash=malloc(20))==NULL) // enough space for a 160-bit hash
    {
        fprintf(stderr, "Memory allocation failed!\n");
        exit(1);
    }
    SHA1(key, strlen(key), keyhash);

    // TASK 2: use the key to create a pseudorandom order to visit the coefficients

    // Generate the random visiting of DCT coefficients
    // This is stored in psuedoRand array

    int maxZ = blocks_high * blocks_wide * 64; 
    
    if ((pseudoRand = malloc(maxZ * sizeof(int))) == NULL)
    {
        fprintf(stderr, "Memory allocation for pseudoRandom DCT coefficient visiting failed!\n");
        exit(1);
    }

    for (int i = 0; i < maxZ; i++) {
        pseudoRand[i] = i;
    }

    srand(*(unsigned int *) keyhash); 
    for (int i = 0; i < maxZ; i++)
    {
        int r = rand() % maxZ;
        unsigned int temp = pseudoRand[r];
        pseudoRand[r] = pseudoRand[i];
        pseudoRand[i] = temp;
    }

    if(mode==EMBED)
    {
        // TASK 3: embed the payload

        int changes = 0;
        int count = 0; 
        int pseudo = 0; 

        for (int i = 0; i < (payload_length + sizeof(unsigned long)) * 8;)
        {
            pseudo = pseudoRand[count];

            block_x = (pseudo / 64) % blocks_wide;
            block_y = (pseudo / 64) / blocks_wide; 

            u = (pseudo % 64) / 8;
            v = (pseudo % 64) % 8; 

            JCOEFPTR block=(cinfo_in.mem->access_virt_barray)((j_common_ptr)&cinfo_in, DCT_blocks[0], block_y, (JDIMENSION)1, FALSE)[0][block_x];

            if (block[u*8+v] != 0)
            {
                if (abs(block[u*8+v]) % 2 == bit_stream[i])
                {
                    i++;
                }
                else
                {
                    if (block[u*8+v] > 0)
                    {
                        block[u*8+v] = block[u*8+v] - 1;
                        changes++;
                        i++;
                    }
                    else
                    {
                        block[u*8+v] = block[u*8+v] + 1;
                        changes++;
                        i++; 
                    }
                } 
                if (block[u*8+v] == 0)
                {
                    i--;
                }
            }
            count++;
        }

        // libjpeg -- write the coefficient block
        jpeg_write_coefficients(&cinfo_out, DCT_blocks);
        jpeg_finish_compress(&cinfo_out);

        free(bit_stream);

        fprintf(stderr, "embedding efficiency = %f\n", payload_length * 8.0 / changes);
    }
    else if(mode==EXTRACT)
    {
        // TASK 4: extact the payload symbols and reconstruct the original bytes

        if ((extract_payload_length = malloc(sizeof(unsigned long) * 8)) == NULL)
        {
            fprintf(stderr, "Malloc allocation for extract_payload_length failed\n"); 
        }

        int pseudo = 0; 
        int count = 0; 

        // get the size of payload first
        for (int i = 0; i < sizeof(unsigned long) * 8;)
        {
            pseudo = pseudoRand[count];

            block_x = (pseudo / 64) % blocks_wide;
            block_y = (pseudo / 64) / blocks_wide; 

            u = (pseudo % 64) / 8;
            v = (pseudo % 64) % 8; 

            JCOEFPTR block=(cinfo_in.mem->access_virt_barray)((j_common_ptr)&cinfo_in, DCT_blocks[0], block_y, (JDIMENSION)1, FALSE)[0][block_x];


            if (block[u*8+v] != 0)
            {
                extract_payload_length[i] = abs(block[u*8+v]) % 2;
                fprintf(stderr, "%d", extract_payload_length[i]);
                i++; 
            }
            count++; 
        }
        
        fprintf(stderr, "\n");  

        unsigned long total = 0;
  
        for (int i = 0; i < sizeof(unsigned long) * 8; i++)
        {
            total |= extract_payload_length[i] * 1 << (sizeof(unsigned long) * 8 - i - 1);
        }

        fprintf(stderr, "\nThe payload extracted length is: %lu", total); 

        /*
        unsigned long total = 0; 
        for (int i = 0; i < sizeof(unsigned long) * 8; i++)
        {
            total = total * 2 + extract_payload_length[i]; 
        }
        */
        // total is in BYTES
        // total BITS to extract is total * 8
        int totalBytes = total; 
        int totalBits = total * 8; 

        if ((extract_payload_bits = malloc(totalBits * sizeof(unsigned long))) == NULL)
        {
            //fprintf(stderr, "Total malloc: %lu\n", total); 
            fprintf(stderr, "Malloc allocation failed for extract_payload_bits\n");
            exit(1);
        }

        for (int i = 0; i < totalBits;)
        {
            pseudo = pseudoRand[count];

            block_x = (pseudo / 64) % blocks_wide;
            block_y = (pseudo / 64) / blocks_wide; 

            u = (pseudo % 64) / 8;
            v = (pseudo % 64) % 8; 

            JCOEFPTR block=(cinfo_in.mem->access_virt_barray)((j_common_ptr)&cinfo_in, DCT_blocks[0], block_y, (JDIMENSION)1, FALSE)[0][block_x];

            if (block[u*8+v] != 0)
            {
                extract_payload_bits[i] = abs(block[u*8+v] % 2);
                i++; 
            }
            count++; 
        }
        fprintf(stderr, "\n");
        /* fprintf(stderr, "PAYLOAD EXTRACT BITS: \n"); 
        for (int i = 0; i < total * 8; i++)
        {
            fprintf(stderr, "%d", extract_payload_bits[i]);
        }
        fprintf(stderr, "\n"); */

        if ((payload_bytes = malloc((totalBytes) + 1)) == NULL)
        {
            fprintf(stderr, "Malloc allocation failed for EXTRACTED payload_bytes\n");
            exit(1);
        }
        
        for (unsigned long i = 0; i < totalBytes; i++)
        {
            unsigned char t = 0; 
            for (unsigned long j = 0; j < 8; j++)
            {
                t = t * 2 + extract_payload_bits[i * 8 + j];
            }
            payload_bytes[i] = t; 
        }
        payload_bytes[totalBytes] = '\0'; 
        

        for (int i = 0; i < totalBytes; i++)
        {
            fprintf(stderr, "%c", payload_bytes[i]); 
        }


        fprintf(stderr, "\n"); 

        //fprintf(stderr, "PRINTING PAYLOAD EXTRACTION\n"); 
 
        //fprintf(stderr, "%s\n", payload_bytes); 
        
        free(extract_payload_bits);
        free(extract_payload_length);
        free(pseudoRand); 
    }


    // example code: prints out all the DCT blocks to stderr, scanned in row order, but does not change them
    // (if "embedding", the cover jpeg was also sent unchanged  to stdout)

    for (block_y=0; block_y<component->height_in_blocks; block_y++)
    {
        for (block_x=0; block_x< component->width_in_blocks; block_x++)
        {
            // this is the magic code which accesses block (block_x,block_y) from the luma component of the image
            JCOEFPTR block=(cinfo_in.mem->access_virt_barray)((j_common_ptr)&cinfo_in, DCT_blocks[0], block_y, (JDIMENSION)1, FALSE)[0][block_x];
            // JCOEFPTR can just be used as an array of 64 ints
            for (u=0; u<8; u++)
            {
                for(v=0; v<8; v++)
                {
                    //         fprintf(stderr, "%3d ", block[u*8+v]);
                }
                //       fprintf(stderr, "\n");
            }

            //     fprintf(stderr, "\n");
        }
    }

    // libjpeg -- finish with the input file and clean up
    jpeg_finish_decompress(&cinfo_in);
    jpeg_destroy_decompress(&cinfo_in);

    // free memory blocks (not actually needed, the OS will do it)
    free(keyhash);
    free(payload_bytes);
}
