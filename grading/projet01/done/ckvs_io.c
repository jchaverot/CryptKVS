/**
 * @file ckvs_io.c
 * @brief c.f. ckvs_io.h
 */
#include <stdio.h>
#include <string.h>
#include "ckvs_utils.h"
#include "ckvs.h"
#include "error.h"
#include <stdint.h>
#include <stdbool.h>
#include "ckvs_io.h"
#include <stdlib.h>

// correcteur : utiliser les macros de error.h peut simplifier le code

// ----------------------------------------------------------------------
int ckvs_open(const char *filename, struct CKVS *ckvs) {
    //check pointers
    if (ckvs == NULL || filename == NULL) {
        //error
        return ERR_INVALID_ARGUMENT;
    }

    //initialize the CKVS database
    memset(ckvs, 0, sizeof(struct CKVS));

    //open the file in read and write binary mode and assign it to the CKVS File
    FILE *file = NULL;
    file = fopen(filename, "r+b");
    if (file == NULL) {
        //error
        return ERR_IO;
    }
    ckvs->file = file;

    // correcteur : plus simple de directement fread le header

    //read the header and that it was well read
    char header_str[CKVS_HEADERSTRINGLEN];
    size_t nb_ok = fread(header_str, sizeof(char), CKVS_HEADERSTRINGLEN, file);
    if (nb_ok != CKVS_HEADERSTRINGLEN) {
        //error
        fclose(file);
        return ERR_IO;
    }
    //read the infos and that they were well read
    uint32_t infos[CKVS_UINT32_T_ELEMENTS] = {0};
    size_t nb_ok2 = fread(infos, sizeof(uint32_t), 4, file);
    if (nb_ok2 != CKVS_UINT32_T_ELEMENTS) {
        //error
        fclose(file);
        return ERR_IO;
    }
    //check that the header start with the good prefix
    if (strncmp(CKVS_HEADERSTRING_PREFIX, header_str, strlen(CKVS_HEADERSTRING_PREFIX)) != 0) {
        //error
        fclose(file);
        return ERR_CORRUPT_STORE;
    }

    if (infos[0] != 1) {
        //error
        fclose(file);
        return ERR_CORRUPT_STORE;
    }

    // correcteur : il faudrait faire une fonction pour que ce soit plus clair (et il y a des méthodes plus simples)
    //check that the table has a size power of 2
    uint32_t table_size = infos[1];
    while (table_size >= 2) {
        if (table_size % 2 != 0) break;
        table_size = table_size / 2;
    }
    if (table_size != 1) {
        //error
        fclose(file);
        return ERR_CORRUPT_STORE;
    }

    //construct the header now that every field is safe
    ckvs_header_t header = {
            .version           =infos[0],
            .table_size        =infos[1],
            .threshold_entries =infos[2],
            .num_entries       =infos[3]
    };
    strcpy(header.header_string, header_str);
    ckvs->header = header;

    //For now but to be modified later
    if (ckvs->header.table_size != CKVS_FIXEDSIZE_TABLE) {
        //error
        fclose(file);
        return ERR_CORRUPT_STORE;
    }

    size_t nb_ok3 = fread(ckvs->entries, sizeof(ckvs_entry_t), CKVS_FIXEDSIZE_TABLE, file);
    if (nb_ok3 != CKVS_FIXEDSIZE_TABLE) {
        //error
        fclose(file);
        return ERR_IO;
    }

    return ERR_NONE;
}

// ----------------------------------------------------------------------
void ckvs_close(struct CKVS *ckvs) {
    //check if the argument is valid, if so exit the function without doing anything
    if (ckvs == NULL) {
        //error
        return;
    }
    //close to file of the CKVS and make it point to NULL
    if (ckvs->file != NULL) {
        fclose(ckvs->file);
    }
    ckvs->file = NULL;
}

// ----------------------------------------------------------------------
int ckvs_find_entry(struct CKVS *ckvs, const char *key, const struct ckvs_sha *auth_key, struct ckvs_entry **e_out) {
    //check pointeurs
    if (ckvs == NULL || key == NULL || auth_key == NULL || e_out == NULL) {
        //error
        return ERR_INVALID_ARGUMENT;
    }

    //booleans created to follow if the key was found in the CKVS database, and if found, check if the auth_key is correct
    bool keyWasFound = false;
    bool authKeyIsCorrect = false;

    //iterate in the array
    for (size_t i = 0; i < CKVS_FIXEDSIZE_TABLE; ++i) {
        if (strncmp(ckvs->entries[i].key, key, CKVS_MAXKEYLEN) == 0) {
            keyWasFound = true;
            if (ckvs_cmp_sha(&ckvs->entries[i].auth_key, auth_key) == 0) {
                authKeyIsCorrect = true;
                *e_out = &ckvs->entries[i];
            }
            break;
        }
    }

    if (!keyWasFound) {
        //error
        return ERR_KEY_NOT_FOUND;
    }
    if (!authKeyIsCorrect) {
        //error
        return ERR_DUPLICATE_ID;
    }

    return ERR_NONE;
}

// ----------------------------------------------------------------------
int read_value_file_content(const char *filename, char **buffer_ptr, size_t *buffer_size) {
    //check pointers
    if (filename == NULL || buffer_ptr == NULL || buffer_size == NULL) {
        //error
        return ERR_INVALID_ARGUMENT;
    }

    //create and open file
    FILE *file = NULL;
    file = fopen(filename, "rb"); //open in read binary mode
    if (file == NULL) {
        //error
        return ERR_IO;
    }
    //place the pointer at the end of the file and check errors
    int err = fseek(file, 0, SEEK_END);
    if (err != ERR_NONE) {
        return ERR_IO;
    }

    //affects the string's length
    size_t size = (size_t) ftell(file); // correcteur : ftell peut retourner une erreur

    //places the pointer at the beginning of the file back
    err = fseek(file, 0, SEEK_SET);
    if (err != ERR_NONE) {
        return ERR_IO;
    }

    //create a buffer and check errors
    *buffer_ptr = calloc(size + 1, sizeof(char)); //so the '\0' char fits
    if (*buffer_ptr == NULL) {
        //error
        return ERR_OUT_OF_MEMORY;
    }

    //read file and check errors
    size_t nb = fread(*buffer_ptr, sizeof(char), size, file);
    if (nb != size) {
        // correcteur : free en cas d'erreur manque
        //error
        return ERR_IO;
    }
    // correcteur : buffer_size se réfère normalement plutôt à la taille du contenu, mais c'est un peu ambigu
    *buffer_size = size + 1; //update the buffer size to have the place for the final '\0'
    // correcteur : pas besoin à cause de calloc
    (*buffer_ptr)[size] = '\0'; //to add the final '\0'

    //close the opened file and finish
    fclose(file);
    return ERR_NONE;
}

// ----------------------------------------------------------------------
int ckvs_write_entry_to_disk(struct CKVS *ckvs, uint32_t idx) {
    //check ckvs pointer and validity of idx value
    if (ckvs == NULL || (idx >= CKVS_FIXEDSIZE_TABLE)) {
        //error
        return ERR_INVALID_ARGUMENT;
    }
    //place the pointer on the file on the right place and check errors
    int err = fseek(ckvs->file, (long int) (idx * sizeof(struct ckvs_entry) + sizeof(ckvs_header_t)), SEEK_SET);
    if (err != ERR_NONE) {
        //error
        return ERR_IO;
    }

    //write the entry and check errors
    size_t nb2 = fwrite(&ckvs->entries[idx], sizeof(ckvs_entry_t), 1, ckvs->file);
    if (nb2 != 1) {
        //error
        return ERR_IO;
    }
    return ERR_NONE;
}

// ----------------------------------------------------------------------
int ckvs_write_encrypted_value(struct CKVS *ckvs, struct ckvs_entry *e, const unsigned char *buf, uint64_t buflen) {
    //check pointers
    if (ckvs == NULL || e == NULL || buf == NULL) {
        //error
        return ERR_INVALID_ARGUMENT;
    }
    //to place the pointer at the end of the ckvs file
    int err = fseek(ckvs->file, 0, SEEK_END);
    if (err != ERR_NONE) {
        //error
        return ERR_IO;
    }

    //to assign the new values of c2, value_off and value_len
    e->value_len = buflen;
    e->value_off = (size_t) ftell(ckvs->file); // correcteur : ftell peut retourner une erreur

    //write at the end of the ckvs file the encrypted value to writes
    size_t nb_ok = fwrite(buf, sizeof(char), buflen, ckvs->file);
    if (nb_ok != buflen) {
        //error
        return ERR_IO;
    }
    err = fflush(ckvs->file);
    if (err != ERR_NONE) {
        //error
        return ERR_IO;
    }

    //to modify the right entry in the ckvs table, index is obtained by substracting the pointers
    uint32_t idx = (uint32_t)(e - &ckvs->entries[0]);

    //modify the entry in the disk
    err = ckvs_write_entry_to_disk(ckvs, idx);
    if (err != ERR_NONE) {
        //error
        return err;
    }

    return ERR_NONE;
}
