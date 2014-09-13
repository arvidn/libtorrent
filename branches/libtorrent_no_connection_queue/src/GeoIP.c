/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 2; tab-width: 2 -*- */
/* GeoIP.c
 *
 * Copyright (C) 2006 MaxMind LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "libtorrent/GeoIP.h"

#include "libtorrent/ConvertUTF.h"

#ifndef WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h> /* For ntohl */
#include <arpa/inet.h>

#include <sys/mman.h>

#else
#include <windows.h>
#include <winsock2.h>
#define snprintf _snprintf
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h> /* for fstat */
#include <sys/stat.h>	/* for fstat */

#ifdef HAVE_STDINT_H
#include <stdint.h>     /* For uint32_t */
#endif

#ifndef        INADDR_NONE
#define        INADDR_NONE     -1
#endif

#define COUNTRY_BEGIN 16776960
#define STATE_BEGIN_REV0 16700000
#define STATE_BEGIN_REV1 16000000
#define STRUCTURE_INFO_MAX_SIZE 20
#define DATABASE_INFO_MAX_SIZE 100
#define MAX_ORG_RECORD_LENGTH 300
#define US_OFFSET 1
#define CANADA_OFFSET 677
#define WORLD_OFFSET 1353
#define FIPS_RANGE 360

#define CHECK_ERR(err, msg) { \
		if (err != Z_OK) { \
				fprintf(stderr, "%s error: %d\n", msg, err); \
				exit(1); \
		} \
}

const char GeoIP_country_code[253][3] = { "--","AP","EU","AD","AE","AF","AG","AI","AL","AM","AN",
	"AO","AQ","AR","AS","AT","AU","AW","AZ","BA","BB",
	"BD","BE","BF","BG","BH","BI","BJ","BM","BN","BO",
	"BR","BS","BT","BV","BW","BY","BZ","CA","CC","CD",
	"CF","CG","CH","CI","CK","CL","CM","CN","CO","CR",
	"CU","CV","CX","CY","CZ","DE","DJ","DK","DM","DO",
	"DZ","EC","EE","EG","EH","ER","ES","ET","FI","FJ",
	"FK","FM","FO","FR","FX","GA","GB","GD","GE","GF",
	"GH","GI","GL","GM","GN","GP","GQ","GR","GS","GT",
	"GU","GW","GY","HK","HM","HN","HR","HT","HU","ID",
	"IE","IL","IN","IO","IQ","IR","IS","IT","JM","JO",
	"JP","KE","KG","KH","KI","KM","KN","KP","KR","KW",
	"KY","KZ","LA","LB","LC","LI","LK","LR","LS","LT",
	"LU","LV","LY","MA","MC","MD","MG","MH","MK","ML",
	"MM","MN","MO","MP","MQ","MR","MS","MT","MU","MV",
	"MW","MX","MY","MZ","NA","NC","NE","NF","NG","NI",
	"NL","NO","NP","NR","NU","NZ","OM","PA","PE","PF",
	"PG","PH","PK","PL","PM","PN","PR","PS","PT","PW",
	"PY","QA","RE","RO","RU","RW","SA","SB","SC","SD",
	"SE","SG","SH","SI","SJ","SK","SL","SM","SN","SO",
	"SR","ST","SV","SY","SZ","TC","TD","TF","TG","TH",
	"TJ","TK","TM","TN","TO","TL","TR","TT","TV","TW",
	"TZ","UA","UG","UM","US","UY","UZ","VA","VC","VE",
	"VG","VI","VN","VU","WF","WS","YE","YT","RS","ZA",
	"ZM","ME","ZW","A1","A2","O1","AX","GG","IM","JE",
  "BL","MF"};

const char GeoIP_country_code3[253][4] = { "--","AP","EU","AND","ARE","AFG","ATG","AIA","ALB","ARM","ANT",
	"AGO","AQ","ARG","ASM","AUT","AUS","ABW","AZE","BIH","BRB",
	"BGD","BEL","BFA","BGR","BHR","BDI","BEN","BMU","BRN","BOL",
	"BRA","BHS","BTN","BV","BWA","BLR","BLZ","CAN","CC","COD",
	"CAF","COG","CHE","CIV","COK","CHL","CMR","CHN","COL","CRI",
	"CUB","CPV","CX","CYP","CZE","DEU","DJI","DNK","DMA","DOM",
	"DZA","ECU","EST","EGY","ESH","ERI","ESP","ETH","FIN","FJI",
	"FLK","FSM","FRO","FRA","FX","GAB","GBR","GRD","GEO","GUF",
	"GHA","GIB","GRL","GMB","GIN","GLP","GNQ","GRC","GS","GTM",
	"GUM","GNB","GUY","HKG","HM","HND","HRV","HTI","HUN","IDN",
	"IRL","ISR","IND","IO","IRQ","IRN","ISL","ITA","JAM","JOR",
	"JPN","KEN","KGZ","KHM","KIR","COM","KNA","PRK","KOR","KWT",
	"CYM","KAZ","LAO","LBN","LCA","LIE","LKA","LBR","LSO","LTU",
	"LUX","LVA","LBY","MAR","MCO","MDA","MDG","MHL","MKD","MLI",
	"MMR","MNG","MAC","MNP","MTQ","MRT","MSR","MLT","MUS","MDV",
	"MWI","MEX","MYS","MOZ","NAM","NCL","NER","NFK","NGA","NIC",
	"NLD","NOR","NPL","NRU","NIU","NZL","OMN","PAN","PER","PYF",
	"PNG","PHL","PAK","POL","SPM","PCN","PRI","PSE","PRT","PLW",
	"PRY","QAT","REU","ROU","RUS","RWA","SAU","SLB","SYC","SDN",
	"SWE","SGP","SHN","SVN","SJM","SVK","SLE","SMR","SEN","SOM",
	"SUR","STP","SLV","SYR","SWZ","TCA","TCD","TF","TGO","THA",
	"TJK","TKL","TKM","TUN","TON","TLS","TUR","TTO","TUV","TWN",
	"TZA","UKR","UGA","UM","USA","URY","UZB","VAT","VCT","VEN",
	"VGB","VIR","VNM","VUT","WLF","WSM","YEM","YT","SRB","ZAF",
	"ZMB","MNE","ZWE","A1","A2","O1","ALA","GGY","IMN","JEY",
  "BLM","MAF"};

const char * GeoIP_country_name[253] = {"N/A","Asia/Pacific Region","Europe","Andorra","United Arab Emirates","Afghanistan","Antigua and Barbuda","Anguilla","Albania","Armenia","Netherlands Antilles",
	"Angola","Antarctica","Argentina","American Samoa","Austria","Australia","Aruba","Azerbaijan","Bosnia and Herzegovina","Barbados",
	"Bangladesh","Belgium","Burkina Faso","Bulgaria","Bahrain","Burundi","Benin","Bermuda","Brunei Darussalam","Bolivia",
	"Brazil","Bahamas","Bhutan","Bouvet Island","Botswana","Belarus","Belize","Canada","Cocos (Keeling) Islands","Congo, The Democratic Republic of the",
	"Central African Republic","Congo","Switzerland","Cote D'Ivoire","Cook Islands","Chile","Cameroon","China","Colombia","Costa Rica",
	"Cuba","Cape Verde","Christmas Island","Cyprus","Czech Republic","Germany","Djibouti","Denmark","Dominica","Dominican Republic",
	"Algeria","Ecuador","Estonia","Egypt","Western Sahara","Eritrea","Spain","Ethiopia","Finland","Fiji",
	"Falkland Islands (Malvinas)","Micronesia, Federated States of","Faroe Islands","France","France, Metropolitan","Gabon","United Kingdom","Grenada","Georgia","French Guiana",
	"Ghana","Gibraltar","Greenland","Gambia","Guinea","Guadeloupe","Equatorial Guinea","Greece","South Georgia and the South Sandwich Islands","Guatemala",
	"Guam","Guinea-Bissau","Guyana","Hong Kong","Heard Island and McDonald Islands","Honduras","Croatia","Haiti","Hungary","Indonesia",
	"Ireland","Israel","India","British Indian Ocean Territory","Iraq","Iran, Islamic Republic of","Iceland","Italy","Jamaica","Jordan",
	"Japan","Kenya","Kyrgyzstan","Cambodia","Kiribati","Comoros","Saint Kitts and Nevis","Korea, Democratic People's Republic of","Korea, Republic of","Kuwait",
	"Cayman Islands","Kazakhstan","Lao People's Democratic Republic","Lebanon","Saint Lucia","Liechtenstein","Sri Lanka","Liberia","Lesotho","Lithuania",
	"Luxembourg","Latvia","Libyan Arab Jamahiriya","Morocco","Monaco","Moldova, Republic of","Madagascar","Marshall Islands","Macedonia","Mali",
	"Myanmar","Mongolia","Macau","Northern Mariana Islands","Martinique","Mauritania","Montserrat","Malta","Mauritius","Maldives",
	"Malawi","Mexico","Malaysia","Mozambique","Namibia","New Caledonia","Niger","Norfolk Island","Nigeria","Nicaragua",
	"Netherlands","Norway","Nepal","Nauru","Niue","New Zealand","Oman","Panama","Peru","French Polynesia",
	"Papua New Guinea","Philippines","Pakistan","Poland","Saint Pierre and Miquelon","Pitcairn Islands","Puerto Rico","Palestinian Territory","Portugal","Palau",
	"Paraguay","Qatar","Reunion","Romania","Russian Federation","Rwanda","Saudi Arabia","Solomon Islands","Seychelles","Sudan",
	"Sweden","Singapore","Saint Helena","Slovenia","Svalbard and Jan Mayen","Slovakia","Sierra Leone","San Marino","Senegal","Somalia","Suriname",
	"Sao Tome and Principe","El Salvador","Syrian Arab Republic","Swaziland","Turks and Caicos Islands","Chad","French Southern Territories","Togo","Thailand",
	"Tajikistan","Tokelau","Turkmenistan","Tunisia","Tonga","Timor-Leste","Turkey","Trinidad and Tobago","Tuvalu","Taiwan",
	"Tanzania, United Republic of","Ukraine","Uganda","United States Minor Outlying Islands","United States","Uruguay","Uzbekistan","Holy See (Vatican City State)","Saint Vincent and the Grenadines","Venezuela",
	"Virgin Islands, British","Virgin Islands, U.S.","Vietnam","Vanuatu","Wallis and Futuna","Samoa","Yemen","Mayotte","Serbia","South Africa",
	"Zambia","Montenegro","Zimbabwe","Anonymous Proxy","Satellite Provider","Other","Aland Islands","Guernsey","Isle of Man","Jersey",
  "Saint Barthelemy","Saint Martin"};

/* Possible continent codes are AF, AS, EU, NA, OC, SA for Africa, Asia, Europe, North America, Oceania
and South America. */

const char GeoIP_country_continent[253][3] = {"--","AS","EU","EU","AS","AS","SA","SA","EU","AS","SA",
	"AF","AN","SA","OC","EU","OC","SA","AS","EU","SA",
	"AS","EU","AF","EU","AS","AF","AF","SA","AS","SA",
	"SA","SA","AS","AF","AF","EU","SA","NA","AS","AF",
	"AF","AF","EU","AF","OC","SA","AF","AS","SA","SA",
	"SA","AF","AS","AS","EU","EU","AF","EU","SA","SA",
	"AF","SA","EU","AF","AF","AF","EU","AF","EU","OC",
	"SA","OC","EU","EU","EU","AF","EU","SA","AS","SA",
	"AF","EU","SA","AF","AF","SA","AF","EU","SA","SA",
	"OC","AF","SA","AS","AF","SA","EU","SA","EU","AS",
	"EU","AS","AS","AS","AS","AS","EU","EU","SA","AS",
	"AS","AF","AS","AS","OC","AF","SA","AS","AS","AS",
	"SA","AS","AS","AS","SA","EU","AS","AF","AF","EU",
	"EU","EU","AF","AF","EU","EU","AF","OC","EU","AF",
	"AS","AS","AS","OC","SA","AF","SA","EU","AF","AS",
	"AF","NA","AS","AF","AF","OC","AF","OC","AF","SA",
	"EU","EU","AS","OC","OC","OC","AS","SA","SA","OC",
	"OC","AS","AS","EU","SA","OC","SA","AS","EU","OC",
	"SA","AS","AF","EU","AS","AF","AS","OC","AF","AF",
	"EU","AS","AF","EU","EU","EU","AF","EU","AF","AF",
	"SA","AF","SA","AS","AF","SA","AF","AF","AF","AS",
	"AS","OC","AS","AF","OC","AS","AS","SA","OC","AS",
	"AF","EU","AF","OC","NA","SA","AS","EU","SA","SA",
	"SA","SA","AS","OC","OC","OC","AS","AF","EU","AF",
	"AF","EU","AF","--","--","--","EU","EU","EU","EU",
  "SA","SA"};

const char * GeoIPDBDescription[NUM_DB_TYPES] = {NULL, "GeoIP Country Edition", "GeoIP City Edition, Rev 1", "GeoIP Region Edition, Rev 1", "GeoIP ISP Edition", "GeoIP Organization Edition", "GeoIP City Edition, Rev 0", "GeoIP Region Edition, Rev 0","GeoIP Proxy Edition","GeoIP ASNum Edition","GeoIP Netspeed Edition","GeoIP Domain Name Edition"};

char * custom_directory = NULL;

void GeoIP_setup_custom_directory (char * dir) {
	custom_directory = dir;
}
/*
char *_GeoIP_full_path_to(const char *file_name) {
	int len;
	char *path = malloc(sizeof(char) * 1024);

	if (custom_directory == NULL){
#ifndef WIN32
		memset(path, 0, sizeof(char) * 1024);
		snprintf(path, sizeof(char) * 1024 - 1, "%s/%s", GEOIPDATADIR, file_name);
#else
		char buf[MAX_PATH], *p, *q = NULL;
		memset(buf, 0, sizeof(buf));
		len = GetModuleFileName(GetModuleHandle(NULL), buf, sizeof(buf) - 1);
		for (p = buf + len; p > buf; p--)
			if (*p == '\\')
				{
					if (!q)
						q = p;
					else
						*p = '/';
				}
		*q = 0;
		memset(path, 0, sizeof(char) * 1024);
		snprintf(path, sizeof(char) * 1024 - 1, "%s/%s", buf, file_name);
#endif
	} else {
		len = strlen(custom_directory);
		if (custom_directory[len-1] != '/') {
			snprintf(path, sizeof(char) * 1024 - 1, "%s/%s",custom_directory, file_name);
		} else {
			snprintf(path, sizeof(char) * 1024 - 1, "%s%s", custom_directory, file_name);
		}
	}
	return path;
}

char ** GeoIPDBFileName = NULL;

void _GeoIP_setup_dbfilename() {
	if (NULL == GeoIPDBFileName) {
		GeoIPDBFileName = malloc(sizeof(char *) * NUM_DB_TYPES);
		memset(GeoIPDBFileName, 0, sizeof(char *) * NUM_DB_TYPES);

		GeoIPDBFileName[GEOIP_COUNTRY_EDITION]		= _GeoIP_full_path_to("GeoIP.dat");
		GeoIPDBFileName[GEOIP_REGION_EDITION_REV0]	= _GeoIP_full_path_to("GeoIPRegion.dat");
		GeoIPDBFileName[GEOIP_REGION_EDITION_REV1]	= _GeoIP_full_path_to("GeoIPRegion.dat");
		GeoIPDBFileName[GEOIP_CITY_EDITION_REV0]	= _GeoIP_full_path_to("GeoIPCity.dat");
		GeoIPDBFileName[GEOIP_CITY_EDITION_REV1]	= _GeoIP_full_path_to("GeoIPCity.dat");
		GeoIPDBFileName[GEOIP_ISP_EDITION]		= _GeoIP_full_path_to("GeoIPISP.dat");
		GeoIPDBFileName[GEOIP_ORG_EDITION]		= _GeoIP_full_path_to("GeoIPOrg.dat");
		GeoIPDBFileName[GEOIP_PROXY_EDITION]		= _GeoIP_full_path_to("GeoIPProxy.dat");
		GeoIPDBFileName[GEOIP_ASNUM_EDITION]		= _GeoIP_full_path_to("GeoIPASNum.dat");
		GeoIPDBFileName[GEOIP_NETSPEED_EDITION]		= _GeoIP_full_path_to("GeoIPNetSpeed.dat");
		GeoIPDBFileName[GEOIP_DOMAIN_EDITION]		= _GeoIP_full_path_to("GeoIPDomain.dat");
	}
}
*/

static
int _file_exists(const char *file_name) {
	struct stat file_stat;
	return( (stat(file_name, &file_stat) == 0) ? 1:0);
}
/*
int GeoIP_db_avail(int type) {
	const char * filePath;
	if (type < 0 || type >= NUM_DB_TYPES) {
		return 0;
	}
	_GeoIP_setup_dbfilename();
	filePath = GeoIPDBFileName[type];
	if (NULL == filePath) {
		return 0;
	}
	return _file_exists(filePath);
}
*/
static
void _setup_segments(GeoIP * gi) {
	int i, j;
	unsigned char delim[3];
	unsigned char buf[SEGMENT_RECORD_LENGTH];

	gi->databaseSegments = NULL;

	/* default to GeoIP Country Edition */
	gi->databaseType = GEOIP_COUNTRY_EDITION;
	gi->record_length = STANDARD_RECORD_LENGTH;
	fseek(gi->GeoIPDatabase, -3l, SEEK_END);
	for (i = 0; i < STRUCTURE_INFO_MAX_SIZE; i++) {
		fread(delim, 1, 3, gi->GeoIPDatabase);
		if (delim[0] == 255 && delim[1] == 255 && delim[2] == 255) {
			fread(&gi->databaseType, 1, 1, gi->GeoIPDatabase);
			if (gi->databaseType >= 106) {
				/* backwards compatibility with databases from April 2003 and earlier */
				gi->databaseType -= 105;
			}

			if (gi->databaseType == GEOIP_REGION_EDITION_REV0) {
				/* Region Edition, pre June 2003 */
				gi->databaseSegments = (unsigned int*)malloc(sizeof(unsigned int));
				gi->databaseSegments[0] = STATE_BEGIN_REV0;
			} else if (gi->databaseType == GEOIP_REGION_EDITION_REV1) {
				/* Region Edition, post June 2003 */
				gi->databaseSegments = (unsigned int*)malloc(sizeof(unsigned int));
				gi->databaseSegments[0] = STATE_BEGIN_REV1;
			} else if (gi->databaseType == GEOIP_CITY_EDITION_REV0 ||
								 gi->databaseType == GEOIP_CITY_EDITION_REV1 ||
								 gi->databaseType == GEOIP_ORG_EDITION ||
								 gi->databaseType == GEOIP_ISP_EDITION ||
								 gi->databaseType == GEOIP_ASNUM_EDITION) {
				/* City/Org Editions have two segments, read offset of second segment */
				gi->databaseSegments = (unsigned int*)malloc(sizeof(unsigned int));
				gi->databaseSegments[0] = 0;
				fread(buf, SEGMENT_RECORD_LENGTH, 1, gi->GeoIPDatabase);
				for (j = 0; j < SEGMENT_RECORD_LENGTH; j++) {
					gi->databaseSegments[0] += (buf[j] << (j * 8));
				}
				if (gi->databaseType == GEOIP_ORG_EDITION ||
						gi->databaseType == GEOIP_ISP_EDITION)
					gi->record_length = ORG_RECORD_LENGTH;
			}
			break;
		} else {
			fseek(gi->GeoIPDatabase, -4l, SEEK_CUR);
		}
	}
	if (gi->databaseType == GEOIP_COUNTRY_EDITION ||
			gi->databaseType == GEOIP_PROXY_EDITION ||
			gi->databaseType == GEOIP_NETSPEED_EDITION) {
		gi->databaseSegments = (unsigned int*)malloc(sizeof(unsigned int));
		gi->databaseSegments[0] = COUNTRY_BEGIN;
	}
}

static
int _check_mtime(GeoIP *gi) {
	struct stat buf;
	if (gi->flags & GEOIP_CHECK_CACHE) {
		if (stat(gi->file_path, &buf) != -1) {
			if (buf.st_mtime != gi->mtime) {
				int name_len;
				wchar_t* wfilename;
				wchar_t const* dst_start;
				char const* src_start;
				/* GeoIP Database file updated */
				if (gi->flags & (GEOIP_MEMORY_CACHE | GEOIP_MMAP_CACHE)) {
#if !defined WIN32 && !defined __OS2__
				    if ( gi->flags & GEOIP_MMAP_CACHE) {
					munmap(gi->cache, gi->size);
					gi->cache = NULL;
				    } else
#endif
					 {
					/* reload database into memory cache */
					if ((gi->cache = (unsigned char*) realloc(gi->cache, buf.st_size)) == NULL) {
						fprintf(stderr,"Out of memory when reloading %s\n",gi->file_path);
						return -1;
					}
				    }
				}
				/* refresh filehandle */
				fclose(gi->GeoIPDatabase);
#ifdef WIN32
				assert(sizeof(wchar_t) == 2);
				name_len = strlen(gi->file_path);
				wfilename = malloc((name_len + 1) * sizeof(wchar_t));
				dst_start = wfilename;
				src_start = gi->file_path;
				ConvertUTF8toUTF16((const UTF8**)&src_start, (const UTF8*)src_start
					+ name_len+1, (UTF16**)&dst_start, (UTF16*)dst_start + name_len + 1
					, lenientConversion);
				gi->GeoIPDatabase = _wfopen(wfilename,L"rb");
				free(wfilename);
#else
				gi->GeoIPDatabase = fopen(gi->file_path,"rb");
#endif
				if (gi->GeoIPDatabase == NULL) {
					fprintf(stderr,"Error Opening file %s when reloading\n",gi->file_path);
					return -1;
				}
				gi->mtime = buf.st_mtime;
				gi->size = buf.st_size;

#if !defined WIN32 && !defined __OS2__
				if ( gi->flags & GEOIP_MMAP_CACHE) {
				    gi->cache = (unsigned char*)mmap(NULL, buf.st_size, PROT_READ, MAP_PRIVATE, fileno(gi->GeoIPDatabase), 0);
				    if ( gi->cache == MAP_FAILED ) {

					    fprintf(stderr,"Error remapping file %s when reloading\n",gi->file_path);
					    gi->cache = 0;
					    return -1;
				    }
				} else
#endif
				if ( gi->flags & GEOIP_MEMORY_CACHE ) {
				    if (fread(gi->cache, sizeof(unsigned char), buf.st_size, gi->GeoIPDatabase) != (size_t) buf.st_size) {
					    fprintf(stderr,"Error reading file %s when reloading\n",gi->file_path);
					    return -1;
					}
				}
				if (gi->databaseSegments != NULL) {
					free(gi->databaseSegments);
					gi->databaseSegments = NULL;
				}
				_setup_segments(gi);
				if (gi->databaseSegments == NULL) {
					fprintf(stderr, "Error reading file %s -- corrupt\n", gi->file_path);
					return -1;
				}
				if (gi->flags & GEOIP_INDEX_CACHE) {                        
					gi->index_cache = (unsigned char *) realloc(gi->index_cache, sizeof(unsigned char) * ((gi->databaseSegments[0] * (long)gi->record_length * 2)));
					if (gi->index_cache != NULL) {
						fseek(gi->GeoIPDatabase, 0, SEEK_SET);
						if (fread(gi->index_cache, sizeof(unsigned char), gi->databaseSegments[0] * (long)gi->record_length * 2, gi->GeoIPDatabase) != (size_t) (gi->databaseSegments[0]*(long)gi->record_length * 2)) {
							fprintf(stderr,"Error reading file %s where reloading\n",gi->file_path);
							return -1;
						}
					}
				}
			}
		}
	}
	return 0;
}

unsigned int _GeoIP_seek_record (GeoIP *gi, unsigned long ipnum) {
	int depth;
	unsigned int x;
	unsigned char stack_buffer[2 * MAX_RECORD_LENGTH];
	const unsigned char *buf = (gi->cache == NULL) ? stack_buffer : NULL;
	unsigned int offset = 0;

	const unsigned char * p;
	int j;

	_check_mtime(gi);
	for (depth = 31; depth >= 0; depth--) {
		if (gi->cache == NULL && gi->index_cache == NULL) {
			/* read from disk */
			fseek(gi->GeoIPDatabase, (long)gi->record_length * 2 * offset, SEEK_SET);
			fread(stack_buffer,gi->record_length,2,gi->GeoIPDatabase);
		} else if (gi->index_cache == NULL) {
			/* simply point to record in memory */
			buf = gi->cache + (long)gi->record_length * 2 *offset;
		} else {
			buf = gi->index_cache + (long)gi->record_length * 2 * offset;
		}

		if (ipnum & (1 << depth)) {
			/* Take the right-hand branch */
			if ( gi->record_length == 3 ) {
				/* Most common case is completely unrolled and uses constants. */
				x =   (buf[3*1 + 0] << (0*8))
					+ (buf[3*1 + 1] << (1*8))
					+ (buf[3*1 + 2] << (2*8));

			} else {
				/* General case */
				j = gi->record_length;
				p = &buf[2*j];
				x = 0;
				do {
					x <<= 8;
					x += *(--p);
				} while ( --j );
			}

		} else {
			/* Take the left-hand branch */
			if ( gi->record_length == 3 ) {
				/* Most common case is completely unrolled and uses constants. */
				x =   (buf[3*0 + 0] << (0*8))
					+ (buf[3*0 + 1] << (1*8))
					+ (buf[3*0 + 2] << (2*8));
			} else {
				/* General case */
				j = gi->record_length;
				p = &buf[1*j];
				x = 0;
				do {
					x <<= 8;
					x += *(--p);
				} while ( --j );
			}
		}

		if (x >= gi->databaseSegments[0]) {
			gi->netmask = 32 - depth;
			return x;
		}
		offset = x;
	}

	/* shouldn't reach here */
	fprintf(stderr,"Error Traversing Database for ipnum = %lu - Perhaps database is corrupt?\n",ipnum);
	return 0;
}

unsigned long
_GeoIP_addr_to_num(const char *addr)
{
	unsigned int    c, octet, t;
	unsigned long   ipnum;
	int             i = 3;

	octet = ipnum = 0;
	while ((c = *addr++)) {
		if (c == '.') {
			if (octet > 255)
				return 0;
			ipnum <<= 8;
			ipnum += octet;
			i--;
			octet = 0;
		} else {
			t = octet;
			octet <<= 3;
			octet += t;
			octet += t;
			c -= '0';
			if (c > 9)
				return 0;
			octet += c;
		}
	}
	if ((octet > 255) || (i != 0))
		return 0;
	ipnum <<= 8;
	return ipnum + octet;
}
/*
GeoIP* GeoIP_open_type (int type, int flags) {
	GeoIP * gi;
	const char * filePath;
	if (type < 0 || type >= NUM_DB_TYPES) {
		printf("Invalid database type %d\n", type);
		return NULL;
	}
	_GeoIP_setup_dbfilename();
	filePath = GeoIPDBFileName[type];
	if (filePath == NULL) {
		printf("Invalid database type %d\n", type);
		return NULL;
	}
	gi = GeoIP_open (filePath, flags);
	return gi;
}

GeoIP* GeoIP_new (int flags) {
	GeoIP * gi;
	_GeoIP_setup_dbfilename();
	gi = GeoIP_open (GeoIPDBFileName[GEOIP_COUNTRY_EDITION], flags);
	return gi;
}
*/

GeoIP* GeoIP_open (const char * filename, int flags) {
	struct stat buf;
	GeoIP * gi;
	size_t len;
#ifdef WIN32
	int name_len;
	wchar_t* wfilename;
	wchar_t const* dst_start;
	char const* src_start;
#endif

	gi = (GeoIP *)malloc(sizeof(GeoIP));
	if (gi == NULL)
		return NULL;
	len = sizeof(char) * (strlen(filename)+1);
	gi->file_path = (char*)malloc(len);
	if (gi->file_path == NULL) {
		free(gi);
		return NULL;
	}
	strncpy(gi->file_path, filename, len);
#ifdef WIN32
	assert(sizeof(wchar_t) == 2);
	name_len = strlen(filename);
	wfilename = malloc((name_len + 1) * sizeof(wchar_t));
	dst_start = wfilename;
	src_start = filename;
	ConvertUTF8toUTF16((const UTF8**)&src_start, (const UTF8*)src_start
		+ name_len+1, (UTF16**)&dst_start, (UTF16*)dst_start + name_len + 1
		, lenientConversion);
	gi->GeoIPDatabase = _wfopen(wfilename,L"rb");
	free(wfilename);
#else
	gi->GeoIPDatabase = fopen(filename,"rb");
#endif
	if (gi->GeoIPDatabase == NULL) {
		fprintf(stderr,"Error Opening file %s\n",filename);
		free(gi->file_path);
		free(gi);
		return NULL;
	} else {
		if (flags & (GEOIP_MEMORY_CACHE | GEOIP_MMAP_CACHE) ) {
			if (fstat(fileno(gi->GeoIPDatabase), &buf) == -1) {
				fprintf(stderr,"Error stating file %s\n",filename);
				free(gi->file_path);
				free(gi);
				return NULL;
			}
			gi->mtime = buf.st_mtime;
			gi->size = buf.st_size;
#if !defined WIN32 && !defined __OS2__
			/* MMAP added my Peter Shipley */
			if ( flags & GEOIP_MMAP_CACHE) {
			    gi->cache = (unsigned char*)mmap(NULL, buf.st_size, PROT_READ, MAP_PRIVATE, fileno(gi->GeoIPDatabase), 0);
			    if ( gi->cache == MAP_FAILED ) {
				fprintf(stderr,"Error mmaping file %s\n",filename);
				free(gi->file_path);
				free(gi);
				return NULL;
			    }
			} else
#endif
			{
			    gi->cache = (unsigned char *) malloc(sizeof(unsigned char) * buf.st_size);

			    if (gi->cache != NULL) {
				if (fread(gi->cache, sizeof(unsigned char), buf.st_size, gi->GeoIPDatabase) != (size_t) buf.st_size) {
					fprintf(stderr,"Error reading file %s\n",filename);
					free(gi->cache);
					free(gi->file_path);
					free(gi);
					return NULL;
				}
			    }
			}
		} else {
			if (flags & GEOIP_CHECK_CACHE) {
				if (fstat(fileno(gi->GeoIPDatabase), &buf) == -1) {
					fprintf(stderr,"Error stating file %s\n",filename);
					free(gi->file_path);
					free(gi);
					return NULL;
				}
				gi->mtime = buf.st_mtime;
			}
			gi->cache = NULL;
		}
		gi->flags = flags;
		gi->charset = GEOIP_CHARSET_ISO_8859_1;

		_setup_segments(gi);
		if (flags & GEOIP_INDEX_CACHE) {                        
			gi->index_cache = (unsigned char *) malloc(sizeof(unsigned char) * ((gi->databaseSegments[0] * (long)gi->record_length * 2)));
			if (gi->index_cache != NULL) {
				fseek(gi->GeoIPDatabase, 0, SEEK_SET);
				if (fread(gi->index_cache, sizeof(unsigned char), gi->databaseSegments[0] * (long)gi->record_length * 2, gi->GeoIPDatabase) != (size_t) (gi->databaseSegments[0]*(long)gi->record_length * 2)) {
					fprintf(stderr,"Error reading file %s\n",filename);
					free(gi->databaseSegments);
					free(gi->index_cache);
					free(gi);
					return NULL;
				}
			}
		} else {
			gi->index_cache = NULL;
		}
		return gi;
	}
}

void GeoIP_delete (GeoIP *gi) {
	if (gi == NULL )
		return;
	if (gi->GeoIPDatabase != NULL)
		fclose(gi->GeoIPDatabase);
	if (gi->cache != NULL) {
#if !defined WIN32 && !defined __OS2__
	    if ( gi->flags & GEOIP_MMAP_CACHE) {
		munmap(gi->cache, gi->size);
	    } else
#endif 
		 {
		free(gi->cache);
	    }
	    gi->cache = NULL;
	}
	if (gi->index_cache != NULL)
		free(gi->index_cache);
	if (gi->file_path != NULL)
		free(gi->file_path);
	if (gi->databaseSegments != NULL)
		free(gi->databaseSegments);
	free(gi);
}

const char *GeoIP_country_code_by_name (GeoIP* gi, const char *name) {
	int country_id;
	country_id = GeoIP_id_by_name(gi, name);
	return (country_id > 0) ? GeoIP_country_code[country_id] : NULL;
}

const char *GeoIP_country_code3_by_name (GeoIP* gi, const char *name) {
	int country_id;
	country_id = GeoIP_id_by_name(gi, name);
	return (country_id > 0) ? GeoIP_country_code3[country_id] : NULL;
}

const char *GeoIP_country_name_by_name (GeoIP* gi, const char *name) {
	int country_id;
	country_id = GeoIP_id_by_name(gi, name);
	return (country_id > 0) ? GeoIP_country_name[country_id] : NULL;
}

unsigned long _GeoIP_lookupaddress (const char *host) {
	unsigned long addr = inet_addr(host);
	struct hostent phe2;
	struct hostent * phe = &phe2;
	char *buf = NULL;
	int buflength = 16384;
	int herr = 0;
	int result = 0;
#ifdef HAVE_GETHOSTBYNAME_R
	buf = malloc(buflength);
#endif
	if (addr == INADDR_NONE) {
#ifdef HAVE_GETHOSTBYNAME_R
		while (1) {
			/* we use gethostbyname_r here because it is thread-safe and gethostbyname is not */
#ifdef GETHOSTBYNAME_R_RETURNS_INT
			result = gethostbyname_r(host,&phe2,buf,buflength,&phe,&herr);
#else
			phe = gethostbyname_r(host,&phe2,buf,buflength,&herr);
#endif
			if (herr != ERANGE)
				break;
			if (result == 0)
				break;
			/* double the buffer if the buffer is too small */
			buflength = buflength * 2;
			buf = realloc(buf,buflength);
		}
#endif
#ifndef HAVE_GETHOSTBYNAME_R
		/* Some systems do not support gethostbyname_r, such as Mac OS X */
		phe = gethostbyname(host);
#endif
		if (!phe || result != 0) {
			free(buf);
			return 0;
		}
		addr = *((unsigned long *) phe->h_addr_list[0]);
	}
#ifdef HAVE_GETHOSTBYNAME_R
	free(buf);
#endif
	return ntohl(addr);
}

int GeoIP_id_by_name (GeoIP* gi, const char *name) {
	unsigned long ipnum;
	int ret;
	if (name == NULL) {
		return 0;
	}
	if (gi->databaseType != GEOIP_COUNTRY_EDITION && gi->databaseType != GEOIP_PROXY_EDITION && gi->databaseType != GEOIP_NETSPEED_EDITION) {
		printf("Invalid database type %s, expected %s\n", GeoIPDBDescription[(int)gi->databaseType], GeoIPDBDescription[GEOIP_COUNTRY_EDITION]);
		return 0;
	}
	if (!(ipnum = _GeoIP_lookupaddress(name)))
		return 0;
	ret = _GeoIP_seek_record(gi, ipnum) - COUNTRY_BEGIN;
	return ret;

}

const char *GeoIP_country_code_by_addr (GeoIP* gi, const char *addr) {
	int country_id;
	country_id = GeoIP_id_by_addr(gi, addr);
	return (country_id > 0) ? GeoIP_country_code[country_id] : NULL;
}

const char *GeoIP_country_code3_by_addr (GeoIP* gi, const char *addr) {
	int country_id;
	country_id = GeoIP_id_by_addr(gi, addr);
	return (country_id > 0) ? GeoIP_country_code3[country_id] : NULL;
	return GeoIP_country_code3[country_id];
}

const char *GeoIP_country_name_by_addr (GeoIP* gi, const char *addr) {
	int country_id;
	country_id = GeoIP_id_by_addr(gi, addr);
	return (country_id > 0) ? GeoIP_country_name[country_id] : NULL;
	return GeoIP_country_name[country_id];
}

const char *GeoIP_country_name_by_ipnum (GeoIP* gi, unsigned long ipnum) {
	int country_id;
	country_id = GeoIP_id_by_ipnum(gi, ipnum);
	return (country_id > 0) ? GeoIP_country_name[country_id] : NULL;
} 

const char *GeoIP_country_code_by_ipnum (GeoIP* gi, unsigned long ipnum) {
	int country_id;
	country_id = GeoIP_id_by_ipnum(gi, ipnum);
	return (country_id > 0) ? GeoIP_country_code[country_id] : NULL;
}

const char *GeoIP_country_code3_by_ipnum (GeoIP* gi, unsigned long ipnum) {
	int country_id;
	country_id = GeoIP_id_by_ipnum(gi, ipnum);
	return (country_id > 0) ? GeoIP_country_code3[country_id] : NULL;
}

int GeoIP_country_id_by_addr (GeoIP* gi, const char *addr) {
	return GeoIP_id_by_addr(gi, addr);
}

int GeoIP_country_id_by_name (GeoIP* gi, const char *host) {
	return GeoIP_id_by_name(gi, host);
}

int GeoIP_id_by_addr (GeoIP* gi, const char *addr) {
	unsigned long ipnum;
	int ret;
	if (addr == NULL) {
		return 0;
	}
	if (gi->databaseType != GEOIP_COUNTRY_EDITION &&
			gi->databaseType != GEOIP_PROXY_EDITION &&
			gi->databaseType != GEOIP_NETSPEED_EDITION) {
		printf("Invalid database type %s, expected %s\n",
					 GeoIPDBDescription[(int)gi->databaseType],
					 GeoIPDBDescription[GEOIP_COUNTRY_EDITION]);
		return 0;
	}
	ipnum = _GeoIP_addr_to_num(addr);
	ret = _GeoIP_seek_record(gi, ipnum) - COUNTRY_BEGIN;
	return ret;
}

int GeoIP_id_by_ipnum (GeoIP* gi, unsigned long ipnum) {
	int ret;
	if (ipnum == 0) {
		return 0;
	}
	if (gi->databaseType != GEOIP_COUNTRY_EDITION && 
			gi->databaseType != GEOIP_PROXY_EDITION &&
			gi->databaseType != GEOIP_NETSPEED_EDITION) {
		printf("Invalid database type %s, expected %s\n",
					 GeoIPDBDescription[(int)gi->databaseType],
					 GeoIPDBDescription[GEOIP_COUNTRY_EDITION]);
		return 0;
	}
	ret = _GeoIP_seek_record(gi, ipnum) - COUNTRY_BEGIN;
	return ret;
}

char *GeoIP_database_info (GeoIP* gi) {
	int i;
	unsigned char buf[3];
	char *retval;
	int hasStructureInfo = 0;

	if(gi == NULL)
		return NULL;

	_check_mtime(gi);
	fseek(gi->GeoIPDatabase, -3l, SEEK_END);

	/* first get past the database structure information */
	for (i = 0; i < STRUCTURE_INFO_MAX_SIZE; i++) {
		fread(buf, 1, 3, gi->GeoIPDatabase);
		if (buf[0] == 255 && buf[1] == 255 && buf[2] == 255) {
			hasStructureInfo = 1;
			break;
		}
		fseek(gi->GeoIPDatabase, -4l, SEEK_CUR);
	}
	if (hasStructureInfo == 1) {
		fseek(gi->GeoIPDatabase, -6l, SEEK_CUR);
	} else {
		/* no structure info, must be pre Sep 2002 database, go back to end */
		fseek(gi->GeoIPDatabase, -3l, SEEK_END);
	}

	for (i = 0; i < DATABASE_INFO_MAX_SIZE; i++) {
		fread(buf, 1, 3, gi->GeoIPDatabase);
		if (buf[0] == 0 && buf[1] == 0 && buf[2] == 0) {
			retval = (char*)malloc(sizeof(char) * (i+1));
			if (retval == NULL) {
				return NULL;
			}
			fread(retval, 1, i, gi->GeoIPDatabase);
			retval[i] = '\0';
			return retval;
		}
		fseek(gi->GeoIPDatabase, -4l, SEEK_CUR);
	}
	return NULL;
}

/* GeoIP Region Edition functions */

void GeoIP_assign_region_by_inetaddr(GeoIP* gi, unsigned long inetaddr, GeoIPRegion *region) {
	unsigned int seek_region;

	/* This also writes in the terminating NULs (if you decide to
	 * keep them) and clear any fields that are not set. */
	memset(region, 0, sizeof(GeoIPRegion));

	seek_region = _GeoIP_seek_record(gi, ntohl(inetaddr));

	if (gi->databaseType == GEOIP_REGION_EDITION_REV0) {
		/* Region Edition, pre June 2003 */
		seek_region -= STATE_BEGIN_REV0;
		if (seek_region >= 1000) {
			region->country_code[0] = 'U';
			region->country_code[1] = 'S';
			region->region[0] = (char) ((seek_region - 1000)/26 + 65);
			region->region[1] = (char) ((seek_region - 1000)%26 + 65);
		} else {
			memcpy(region->country_code, GeoIP_country_code[seek_region], 2);
		}
	} else if (gi->databaseType == GEOIP_REGION_EDITION_REV1) {
		/* Region Edition, post June 2003 */
		seek_region -= STATE_BEGIN_REV1;
		if (seek_region < US_OFFSET) {
			/* Unknown */
			/* we don't need to do anything here b/c we memset region to 0 */
		} else if (seek_region < CANADA_OFFSET) {
			/* USA State */
			region->country_code[0] = 'U';
			region->country_code[1] = 'S';
			region->region[0] = (char) ((seek_region - US_OFFSET)/26 + 65);
			region->region[1] = (char) ((seek_region - US_OFFSET)%26 + 65);
		} else if (seek_region < WORLD_OFFSET) {
			/* Canada Province */
			region->country_code[0] = 'C';
			region->country_code[1] = 'A';
			region->region[0] = (char) ((seek_region - CANADA_OFFSET)/26 + 65);
			region->region[1] = (char) ((seek_region - CANADA_OFFSET)%26 + 65);
		} else {
			/* Not US or Canada */
			memcpy(region->country_code, GeoIP_country_code[(seek_region - WORLD_OFFSET) / FIPS_RANGE], 2);
		}
	}
}

static
GeoIPRegion * _get_region(GeoIP* gi, unsigned long ipnum) {
	GeoIPRegion * region;
 
	region = (GeoIPRegion*)malloc(sizeof(GeoIPRegion));
	if (region) {
		GeoIP_assign_region_by_inetaddr(gi, htonl(ipnum), region);
	}
	return region;
}

GeoIPRegion * GeoIP_region_by_addr (GeoIP* gi, const char *addr) {
	unsigned long ipnum;
	if (addr == NULL) {
		return 0;
	}
	if (gi->databaseType != GEOIP_REGION_EDITION_REV0 &&
			gi->databaseType != GEOIP_REGION_EDITION_REV1) {
		printf("Invalid database type %s, expected %s\n", GeoIPDBDescription[(int)gi->databaseType], GeoIPDBDescription[GEOIP_REGION_EDITION_REV1]);
		return 0;
	}
	ipnum = _GeoIP_addr_to_num(addr);
	return _get_region(gi, ipnum);
}

GeoIPRegion * GeoIP_region_by_name (GeoIP* gi, const char *name) {
	unsigned long ipnum;
	if (name == NULL) {
		return 0;
	}
	if (gi->databaseType != GEOIP_REGION_EDITION_REV0 &&
			gi->databaseType != GEOIP_REGION_EDITION_REV1) {
		printf("Invalid database type %s, expected %s\n", GeoIPDBDescription[(int)gi->databaseType], GeoIPDBDescription[GEOIP_REGION_EDITION_REV1]);
		return 0;
	}
	if (!(ipnum = _GeoIP_lookupaddress(name)))
		return 0;
	return _get_region(gi, ipnum);
}

GeoIPRegion * GeoIP_region_by_ipnum (GeoIP* gi, unsigned long ipnum) {
	if (gi->databaseType != GEOIP_REGION_EDITION_REV0 &&
			gi->databaseType != GEOIP_REGION_EDITION_REV1) {
		printf("Invalid database type %s, expected %s\n", GeoIPDBDescription[(int)gi->databaseType], GeoIPDBDescription[GEOIP_REGION_EDITION_REV1]);
		return 0;
	}
	return _get_region(gi, ipnum);
}

void GeoIPRegion_delete (GeoIPRegion *gir) {
	free(gir);
}

/* GeoIP Organization, ISP and AS Number Edition private method */
static
char *_get_name (GeoIP* gi, unsigned long ipnum) {
	int seek_org;
	char buf[MAX_ORG_RECORD_LENGTH];
	char * org_buf, * buf_pointer;
	int record_pointer;
	size_t len;

	if (gi->databaseType != GEOIP_ORG_EDITION &&
			gi->databaseType != GEOIP_ISP_EDITION &&
			gi->databaseType != GEOIP_ASNUM_EDITION) {
		printf("Invalid database type %s, expected %s\n", GeoIPDBDescription[(int)gi->databaseType], GeoIPDBDescription[GEOIP_ORG_EDITION]);
		return 0;
	}

	seek_org = _GeoIP_seek_record(gi, ipnum);
	if (seek_org == gi->databaseSegments[0])		
		return NULL;

	record_pointer = seek_org + (2 * gi->record_length - 1) * gi->databaseSegments[0];

	if (gi->cache == NULL) {
		fseek(gi->GeoIPDatabase, record_pointer, SEEK_SET);
		fread(buf, sizeof(char), MAX_ORG_RECORD_LENGTH, gi->GeoIPDatabase);
		len = sizeof(char) * (strlen(buf)+1);
		org_buf = (char*)malloc(len);
		strncpy(org_buf, buf, len);
	} else {
		buf_pointer = (char*)(gi->cache + (long)record_pointer);
		len = sizeof(char) * (strlen(buf_pointer)+1);
		org_buf = (char*)malloc(len);
		strncpy(org_buf, buf_pointer, len);
	}
	return org_buf;
}

char *GeoIP_name_by_ipnum (GeoIP* gi, unsigned long ipnum) {
	return _get_name(gi,ipnum);  
}

char *GeoIP_name_by_addr (GeoIP* gi, const char *addr) {
	unsigned long ipnum;
	if (addr == NULL) {
		return 0;
	}
	ipnum = _GeoIP_addr_to_num(addr);
	return _get_name(gi, ipnum);
}

char *GeoIP_name_by_name (GeoIP* gi, const char *name) {
	unsigned long ipnum;
	if (name == NULL) {
		return 0;
	}
	if (!(ipnum = _GeoIP_lookupaddress(name)))
		return 0;
	return _get_name(gi, ipnum);
}

char *GeoIP_org_by_ipnum (GeoIP* gi, unsigned long ipnum) {
	return GeoIP_name_by_ipnum(gi, ipnum);
}

char *GeoIP_org_by_addr (GeoIP* gi, const char *addr) {
	return GeoIP_name_by_addr(gi, addr);
}

char *GeoIP_org_by_name (GeoIP* gi, const char *name) {
	return GeoIP_name_by_name(gi, name);
}

unsigned char GeoIP_database_edition (GeoIP* gi) {
	return gi->databaseType;
}

int GeoIP_charset( GeoIP* gi){
  return gi->charset;
}

int GeoIP_set_charset(  GeoIP* gi, int charset ){
  int old_charset = gi->charset;
  gi->charset = charset;
  return old_charset;
}

int GeoIP_last_netmask (GeoIP* gi) {
  return gi->netmask;
}

