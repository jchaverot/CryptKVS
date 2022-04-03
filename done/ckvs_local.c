/**
 * @file ckvs_local.c
 * @brief c.f. ckvs_local.h
 */
#include <stdio.h>
#include <string.h>
#include "ckvs_utils.h"
#include "ckvs.h"
#include "error.h"
#include <stdint.h>
#include "ckvs_io.h"
#include "ckvs_crypto.h"
#include "util.h"
#include "openssl/evp.h"
#include "openssl/rand.h"
#include <ctype.h>
#include "ckvs_local.h"

#define C2_SIZE  32
// ----------------------------------------------------------------------
//to make things clearer, instead of using O or 1 when calling ckvs_client_crypt_value
enum crypt_type {
    DECRYPTION,
    ENCRYPTION
};

// ----------------------------------------------------------------------
int ckvs_local_stats(const char *filename) {
    //check if the argument is valid
    if (filename == NULL) return ERR_INVALID_ARGUMENT;
    //initialiaze the struct
    struct CKVS ckvs;
    memset(&ckvs, 0, sizeof(struct CKVS));
    //open the filename and listen to errors
    int r = ckvs_open(filename, &ckvs);
    if (r != ERR_NONE) {
        return r;
    }
    //to print the header of the ckvs read from the file
    print_header(&(ckvs.header));

    //to print the entries of the ckvs read from the file
    for (int i = 0; i < CKVS_FIXEDSIZE_TABLE; i++) {
        if (strlen(ckvs.entries[i].key) != 0) {
            print_entry(&ckvs.entries[i]);
        }
    }

    return ERR_NONE;
}

// ----------------------------------------------------------------------
int ckvs_local_getset(const char *filename, const char *key, const char *pwd, const char *set_value) {
    //check if the arguments are valid
    if (key == NULL || pwd == NULL || filename == NULL) return ERR_INVALID_ARGUMENT;

    //pps_printf("%llu", strlen(set_value));
    //initialize the struct
    struct CKVS ckvs;
    memset(&ckvs, 0, sizeof(struct CKVS));

    //open the file
    int err = ckvs_open(filename, &ckvs);

    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        return err;
    }

    //initialize the struct ckvs_memrecord_t
    ckvs_memrecord_t ckvs_mem;
    memset(&ckvs_mem, 0, sizeof(ckvs_memrecord_t));

    //to generate in particular the auth_key and c1 and store them in ckvs_mem
    err = ckvs_client_encrypt_pwd(&ckvs_mem, key, pwd);

    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        pps_printf("Error 1");

        return err;
    }

    //initialize the struct ckvs_entry_t
    ckvs_entry_t* ckvs_out;
    memset(&ckvs_out, 0, sizeof(ckvs_entry_t *));

    //to find the right entry in the database with the key and the auth_key latterly computed
    err = ckvs_find_entry(&ckvs, key, &ckvs_mem.auth_key, &ckvs_out);

    if (err != ERR_NONE) {
        // Error
        pps_printf("Error 2");
        ckvs_close(&ckvs);
        return err;
    }

    if (set_value != NULL) {
        err = RAND_bytes(&ckvs_out->c2.sha, C2_SIZE);
        if (err != 1) {
            pps_printf("Error 3");
            return ERR_IO;
        }
    }

    //now we have the entry and hence c2, to compute the masterkey
    err = ckvs_client_compute_masterkey(&ckvs_mem, &ckvs_out->c2);
    if (err != ERR_NONE) {
        // Error
        pps_printf("Error 4");
        ckvs_close(&ckvs);
        return err;
    }
    if (set_value == NULL) { //Get part

        //to make the pointer lead to the beginning of the encrypted secret
        fseek(ckvs.file, (long int) ckvs_out->value_off, SEEK_SET);

        //initialize the string where the encrypted secret will be stored
        unsigned char encrypted[ckvs_out->value_len];
        //to read the encrypted secret
       // pps_printf("value_off: %llu\n",ckvs_out->value_off);

        size_t nb_ok = fread(encrypted, sizeof(unsigned char), ckvs_out->value_len, ckvs.file);

        if (nb_ok != ckvs_out->value_len) {
            //pps_printf("nb_ok: %zu, value_len: %llu \n",nb_ok,ckvs_out->value_len);
            ckvs_close(&ckvs);

            return ERR_IO;
        }
        //initialize the string where the decrypted secret will be stored
        size_t decrypted_len = ckvs_out->value_len + EVP_MAX_BLOCK_LENGTH;
        unsigned char decrypted[decrypted_len];
        //decrypts the string with the secret with in particular the master_key stored in ckvs_mem
        err = ckvs_client_crypt_value(&ckvs_mem, DECRYPTION, encrypted, ckvs_out->value_len, decrypted,
                                      &decrypted_len);
        if (err != ERR_NONE) {
            // Error
            ckvs_close(&ckvs);
            free_sve(NULL, NULL);
            pps_printf("Error 6");


            return err;
        }
        //check if we have to end the lecture
        for (size_t i = 0; i < strlen((char *) decrypted); ++i) {

            if ((iscntrl(decrypted[i]) && decrypted[i] != '\n')) break;
            pps_printf("%c", decrypted[i]);
        }
        //pps_printf("%s",decrypted);


        //close the CKVS database at filename since done decrypting
        ckvs_close(&ckvs);
        return ERR_NONE;
    }

    //encrypts set_value content
    size_t set_value_encrypted_length = strlen(set_value) +1 + EVP_MAX_BLOCK_LENGTH;//don't forget the final '\0'
    unsigned char *set_value_encrypted = calloc(set_value_encrypted_length, sizeof(unsigned char));
    err = ckvs_client_crypt_value(&ckvs_mem, ENCRYPTION, (const unsigned char *) set_value, strlen(set_value)+1,
                                  set_value_encrypted,
                                  &set_value_encrypted_length);
    //pps_printf("%d \n",set_value_encrypted_length);
    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        free_sve(&set_value_encrypted, &set_value_encrypted_length);
        return err;
    }
    err = ckvs_write_encrypted_value(&ckvs, ckvs_out, (const unsigned char *) set_value_encrypted,
                                     (uint64_t) set_value_encrypted_length);
    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        free_sve(&set_value_encrypted, &set_value_encrypted_length);
        return err;
    }
    //close the file and terminate
    ckvs_close(&ckvs);
    //pps_printf("%d \n",set_value_encrypted_length);
    free_sve( &set_value_encrypted, &set_value_encrypted_length);

    return ERR_NONE;
}

//-----------------------------------------------------------------------
void free_sve(unsigned char **sve, size_t *sve_length) {
    if (sve != NULL && *sve != NULL) {
        free(*sve);
        *sve = NULL;
    }
    if (sve_length != NULL) *sve_length = 0;
}

// ----------------------------------------------------------------------
int ckvs_local_get(const char *filename, const char *key, const char *pwd) {
    /*
    //check if the arguments are valid
    if (key == NULL || pwd == NULL || filename == NULL) return ERR_INVALID_ARGUMENT;

    //initialize the struct
    struct CKVS ckvs;
    memset(&ckvs, 0, sizeof(struct CKVS));

    //open the file
    int err = ckvs_open(filename, &ckvs);
    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        return err;
    }
    //initialize the struct ckvs_memrecord_t
    ckvs_memrecord_t ckvs_mem;
    memset(&ckvs_mem, 0, sizeof(ckvs_memrecord_t));

    //to generate in particular the auth_key and c1 and store them in ckvs_mem
    err = ckvs_client_encrypt_pwd(&ckvs_mem, key, pwd);

    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        return err;
    }

    //initialize the struct ckvs_entry_t
    ckvs_entry_t *ckvs_out;
    memset(&ckvs_out, 0, sizeof(ckvs_entry_t *));

    //to find the right entry in the database with the key and the auth_key latterly computed
    err = ckvs_find_entry(&ckvs, key, &ckvs_mem.auth_key, &ckvs_out);

    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        return err;
    }

    //now we have the entry and hence c2, to compute the masterkey
    err = ckvs_client_compute_masterkey(&ckvs_mem, &ckvs_out->c2);
    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        return err;
    }

    //to make the pointer lead to the beginning of the encrypted secret
    fseek(ckvs.file, (long int) ckvs_out->value_off, SEEK_SET);

    //initialize the string where the encrypted secret will be stored
    unsigned char encrypted[ckvs_out->value_len];
    //to read the encrypted secret
    size_t nb_ok = fread(encrypted, sizeof(unsigned char), ckvs_out->value_len, ckvs.file);
    if (nb_ok != ckvs_out->value_len) {
        ckvs_close(&ckvs);
        return ERR_IO;
    }
    //initialize the string where the decrypted secret will be stored
    size_t decrypted_len = ckvs_out->value_len + EVP_MAX_BLOCK_LENGTH;
    unsigned char decrypted[decrypted_len];
    //decrypts the string with the secret with in particular the master_key stored in ckvs_mem
    err = ckvs_client_crypt_value(&ckvs_mem, 0, encrypted, ckvs_out->value_len, decrypted,
                                  &decrypted_len);
    if (err != ERR_NONE) {
        // Error
        ckvs_close(&ckvs);
        return err;
    }
    //check if we have to end the lecture
    for (size_t i = 0; i < strlen((char *) decrypted); ++i) {

        if ((iscntrl(decrypted[i]) && decrypted[i] != '\n')) break;
        pps_printf("%c", decrypted[i]);
    }

    //close the CKVS database at filename since done decrypting
    ckvs_close(&ckvs);

    return ERR_NONE;
     */
    return ckvs_local_getset(filename, key, pwd, NULL);
}

// ----------------------------------------------------------------------
int ckvs_local_set(const char *filename, const char *key, const char *pwd, const char *valuefilename) {
    //check pointers
    if (filename == NULL || key == NULL || pwd == NULL || valuefilename == NULL) return ERR_INVALID_ARGUMENT;

    //initialize buffer and its size
    char *buffer = NULL;
    size_t buffer_size = 0;
    
    //reads file called filename and prints it in the buffer
    int err = read_value_file_content(valuefilename, &buffer, &buffer_size);
    //checks errors
    if (err != ERR_NONE) return err;

    //pps_printf("bffer: %s",buffer);
    //called the modularized funciton ckvs_local_getset with the buffer
    err = ckvs_local_getset(filename, key, pwd, buffer);

    free(buffer);
    buffer = NULL;
    return err;
}


