#include "test_helpers.h"
#include "common.h"

int open_session(token_info *info) {
    CK_FUNCTION_LIST_PTR function_pointer = info->function_pointer;
    CK_RV rv;

    rv = function_pointer->C_OpenSession(info->slot_id, CKF_SERIAL_SESSION | CKF_RW_SESSION, NULL_PTR, NULL_PTR,
                                         &info->session_handle);

    if(rv != CKR_OK)
        return 1;

    debug_print("Session was successfully created");
    return 0;
}

int clear_token() {
    debug_print("Clearing token");


    int error = system("pkcs15-init -ET");

    if(error) {
        fprintf(stderr,"Could not erase token!\n");
        return error;
    }

    error = system("pkcs15-init -CT --no-so-pin");

    if(error) {
        fprintf(stderr,"Could not init token!\n");
        return error;
    }

    return 0;
}

int init_token_with_default_pin(const token_info *info) {

    CK_UTF8CHAR so_pin[] = {"00000000"};
    CK_UTF8CHAR new_pin[] = {"12345"};

    CK_FUNCTION_LIST_PTR function_pointer = info->function_pointer;
    CK_RV rv;

    debug_print("Logging in as SO user");
    rv = function_pointer->C_Login(info->session_handle, CKU_SO, so_pin, sizeof(so_pin) - 1);
    if (rv != CKR_OK) {
        fprintf(stderr,"Could not log in to token as SO user!\n");
        return 1;
    }

    debug_print("Initialization of user PIN. Session handle is: %d. New PIN is: %s",info->session_handle, new_pin);
    rv = function_pointer->C_InitPIN(info->session_handle, new_pin, sizeof(new_pin) - 1);
    if (rv != CKR_OK) {
        fprintf(stderr,"Could not init user PIN!\n");
        return 1;
    }

    debug_print("Logging out SO user");
    rv = function_pointer->C_Logout(info->session_handle);
    if (rv != CKR_OK) {
        fprintf(stderr,"Could not log out SO user!\n");
        return 1;
    }

    return 0;
}

int group_setup(void **state)
{

    if(clear_token())
        fail_msg("Could not clear token!\n");

    token_info* info = malloc(sizeof(token_info));

    assert_non_null(info);

    if (load_pkcs11_module(info, library_path)) {
        free(info);
        fail_msg("Could not load module!\n");
    }

    *state = info;
    return 0;
}

int group_teardown(void **state) {

    debug_print("Clearing state after group tests!");
    token_info *info = (token_info *) *state;
    if(info && info->function_pointer)
        info->function_pointer->C_Finalize(NULL_PTR);

    free(info);
    free(library_path);

    close_pkcs11_module();

    return 0;
}

int clear_token_without_login_setup(void **state) {

    if(clear_token())
        fail_msg("Could not clear token!\n");

    token_info *info = (token_info *) *state;

    if(open_session(info))
        fail_msg("Could not open session to token!\n");

    return 0;
}

int clear_token_with_user_login_setup(void **state) {
    token_info *info = (token_info *) *state;
    CK_FUNCTION_LIST_PTR function_pointer = info->function_pointer;
    CK_RV rv;
    CK_UTF8CHAR pin[] = {"12345"};

    if(clear_token())
        fail_msg("Could not clear token!\n");

    if(open_session(info))
        fail_msg("Could not open session to token!\n");


    debug_print("Init token with default PIN and log in as user");
    if(init_token_with_default_pin(info))
        fail_msg("Could not initialize token with default user PIN\n");


    rv = function_pointer->C_Login(info->session_handle, CKU_USER, pin, sizeof(pin) - 1);

    if(rv != CKR_OK)
        fail_msg("Could not login to token with user PIN '%s'\n", pin);

    return 0;
}

int after_test_cleanup(void **state) {

    token_info *info = (token_info *) *state;
    CK_FUNCTION_LIST_PTR function_pointer = info->function_pointer;

    debug_print("Logging out from token");
    function_pointer->C_Logout(info->session_handle);

    info->session_handle = 0;
    debug_print("Closing all sessions");
    function_pointer->C_CloseAllSessions(info->slot_id);

    if(clear_token())
        fail_msg("Could not clear token!\n");
}

CK_BYTE* hex_string_to_byte_array(char* hex_string) {

    int length = strlen(hex_string) / 2;
    CK_BYTE *hex_array = (CK_BYTE*) malloc(length * sizeof(CK_BYTE));
    if(!hex_array)
        return NULL;

    for(int i = 0; i < length; i++) {
        sscanf(hex_string+2*i, "%2X", &hex_array[i]);
    }

    return hex_array;
}

int short_message_digest(const token_info *info, CK_MECHANISM *digest_mechanism,
                         CK_BYTE *hash, CK_ULONG *hash_length) {
    CK_FUNCTION_LIST_PTR function_pointer = info->function_pointer;
    CK_RV rv;


    debug_print("Creating hash of message '%s'", SHORT_MESSAGE_TO_HASH);

    rv = function_pointer->C_DigestInit(info->session_handle, digest_mechanism);
    if (rv != CKR_OK) {
        fprintf(stderr, "Could not init digest algorithm.\n");
        return 1;
    }

    rv = function_pointer->C_Digest(info->session_handle, SHORT_MESSAGE_TO_HASH, strlen(SHORT_MESSAGE_TO_HASH), hash, hash_length);
    if (rv != CKR_OK) {
        fprintf(stderr, "Could not create hash of message.\n");
        return 1;
    }

    debug_print("Hash of message '%s'\n\t has length %d", SHORT_MESSAGE_TO_HASH, (*hash_length));
    return 0;
}

int long_message_digest(const token_info *info, CK_MECHANISM *digest_mechanism, CK_BYTE *hash, CK_ULONG *hash_length) {

    CK_FUNCTION_LIST_PTR function_pointer = info->function_pointer;
    CK_RV rv;
    FILE *fs;

    CK_ULONG data_length = BUFFER_SIZE;
    char input_buffer[BUFFER_SIZE];

    /* Open the input file */
    if ((fs = fopen(LONG_MESSAGE_TO_HASH_PATH, "r")) == NULL) {
        fprintf(stderr, "Could not open file '%s' for reading\n", LONG_MESSAGE_TO_HASH_PATH);
        return 1;
    }

    debug_print("Creating hash of message in file '%s'", LONG_MESSAGE_TO_HASH_PATH);

    rv = function_pointer->C_DigestInit(info->session_handle, digest_mechanism);
    if (rv != CKR_OK) {
        fprintf(stderr, "Could not init digest algorithm.\n");
        fclose(fs);
        return 1;
    }


    /* Read in the data and create digest of this portion */
    while (!feof(fs) && (data_length = fread(input_buffer, 1, BUFFER_SIZE, fs)) > 0) {
        rv = function_pointer->C_DigestUpdate(info->session_handle, (CK_BYTE_PTR) input_buffer, data_length);

        if (rv  != CKR_OK) {
            fprintf(stderr, "Error while calling DigestUpdate function\n");
            fclose(fs);
            return 1;
        }
    }

    /* Get complete digest */
    rv = function_pointer->C_DigestFinal(info->session_handle, hash, hash_length);

    if (rv != CKR_OK) {
        fprintf(stderr, "Could not finish digest!\n");
        fclose(fs);
        return 1;
    }

    fclose(fs);
    debug_print("Hash of message in file '%s'\n\t has length %d", LONG_MESSAGE_TO_HASH_PATH, (*hash_length));
    return 0;
}