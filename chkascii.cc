#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <string>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "assertz.h"
#include "printz.h"
#include "stringz.h"

void usage()
{
    pre("Usage: chkascii [--accept=<ASCII1>[,<ASCII2>]] [--summary] <file>");
    exit(1);
}

struct asciiaccept
{
    int ascii;
    asciiaccept* next;
};

// Parse a string of the form LEAD=sz1[,sz2] to extract substrings sz<*>
int parse(const char* p, std::string** args)
{
    assert_ptr(p, __LINE__);
    assert_nz(*p, __LINE__);

    const char* p2 = strchr(p, '=');

    assert_ptr(p2, __LINE__, (long) &usage);
    p2++;

    if (! *p2)
    {
        *args = 0;
        return 0;
    }

    const char* p3 = p2;
    int commas = 0;

    while ((p3 = strchr(p3, ',')))
    {
        commas++;
        p3++;
    }

    p3 = p2;
    *args = new std::string[commas+1];

    for (int i = 0; i < commas+1; i++)
    {
        assert_ptr(p3, __LINE__);
        char buffer[256];

        strcpy(buffer, p3);
        buffer[sizeof(buffer)-1] = 0;

        char* p4 = strchr(buffer, ',');

        if (p4)
        {
            *p4 = 0;
        }

        (*args)[i] = buffer;
        p3 = strchr(p3, ',');
        p3++;
    }

    return (commas+1);
}

asciiaccept* newnode(asciiaccept** pp, const std::string* psz)
{
    assert_ptr(pp, __LINE__);
    assert_ptr(psz, __LINE__);

    char buffer[256];

    strcpy(buffer, (*psz).c_str());
    buffer[sizeof(buffer)-1] = 0;
    assert_nz(*buffer, __LINE__, (long) &usage);
    assert_num(buffer, true, (long) &usage);

    *pp = new asciiaccept;
    (*pp)->next = 0;
    (*pp)->ascii = atoi(buffer);

    return *pp;
}

bool chkchar(int ch, int* plf, const asciiaccept* pacc)
{
    bool b_bad = true;
    const int specials[] = { 9, 10 };

    if ((ch > 31) && (ch < 127))
    {
        b_bad = false;
    }
    else
    {
        if ((plf) && (ch == 10))
        {
             (*plf)++;
        }

        for (int i = 0; i < sizeof(specials)/sizeof(specials[0]); i++)
        {
            if (ch == specials[i])
            {
                b_bad = false;
                break;
            }
        }

        while (pacc)
        {
            if (ch == pacc->ascii)
            {
                b_bad = false;
                break;
            }

            pacc = pacc->next;
        }
    }

    return b_bad;
}

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        usage();
    }

    struct stat filestat;
    char* filepath = 0;
    int filepathlen = 1024;

    const char* ACCEPT = "--accept=";
    const int len_ACCEPT = strlen(ACCEPT);

    const char* SUMMARY = "--summary";
    const int len_SUMMARY = strlen(SUMMARY);

    argc--;

    const int BLOCKSIZE = 512;
    asciiaccept* pHead = 0;
    bool b_summary = false;

    while (argc > 0)
    {
        if (strncmp(argv[argc], ACCEPT, len_ACCEPT) == 0)
        {
            asciiaccept* pNode = 0;
            char argbuffer[16];
            std::string* psz;
            int z = 0;

            int accs = parse(argv[argc], &psz);
            assert_nz(accs, __LINE__, (long) &usage);
            pHead = newnode(&pNode, &psz[z++]);

            while (z < accs)
            {
                pNode = newnode(&(pNode->next), &psz[z++]);
            }

            delete[] psz;
        }
        else if (strcmp(argv[argc], SUMMARY) == 0)
        {
            b_summary = true;
        }
        else if (stat(argv[argc], &filestat) == 0)
        {
            assert_nz(S_ISREG(filestat.st_mode), __LINE__, (long) &usage);

			//Before allocating, delete any existing memory
			delete[] filepath;
			filepath = 0;

            filepath = new(std::nothrow) char[filepathlen];
            assert_ptr(filepath, __LINE__);

            strcpy(filepath, argv[argc]);
            filepath[filepathlen-1] = 0;
        }
        else
        {
            usage();
        }

        argc--;
    }

    assert_ptr(filepath, __LINE__, (long) &usage);

    const int len = filestat.st_size;
    int fd = open(filepath, O_RDONLY);

    int i = 0;
    int lf = 0;
    int loops = 0;
    int to_read = 0;
    int post_lf = 0;
    int baddies = 0;
    int firstbadoffset = 0;

    unsigned char block[BLOCKSIZE];
    unsigned char retval = 0;

    while (i < len)
    {
        int j = 0;
        int k = 0;

        loops++;
        to_read = (len - i < BLOCKSIZE) ? len - i : BLOCKSIZE;
        memset(block, 0, sizeof(block));

        while (j < to_read)
        {
            j += read(fd, (unsigned char*) (block + j), (to_read - j));
        }

        for (k = 0; k < to_read; k++)
        {
            unsigned char ch = block[k];

            int lf_in = lf;
            bool is_bad = chkchar(ch, &lf, pHead);
            post_lf = (lf_in == lf) ? post_lf + 1 : 0;

            if (is_bad)
            {
                retval = 1;
                int X = ((loops - 1)*(sizeof(block))) + k + 1;

                if (! b_summary)
                {
                    pre("Bad ascii at offset %d", X);
                    pre("Coordinates : line %d ; column %d", lf+1, post_lf);
                    pre("ASCII = %d ; character = %c", ch, ch);
                    close(fd);
                    return retval;
                }
                else
                {
                    if (! baddies)
                    {
                        firstbadoffset = X;
                    }

                    baddies++;
                }
            }
        }

        i += to_read;
    }

    close(fd);

    if (i)
    {
        assert_nz(to_read, __LINE__);
        unsigned char last = block[to_read - 1];

        if (last != 10)
        {
            pre("<file> is NOEOL");
            retval = retval | (1<<7);
        }
    }

    if (b_summary)
    {
        pro("Bad ASCII chars/Total bytes = %d/%d", baddies, len);

        if (baddies)
        {
            pro("First bad ASCII char at offest %d", firstbadoffset);
        }
    }

    return retval;
}