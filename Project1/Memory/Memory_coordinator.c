/* Memory Coordinator */
#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>

#define NANOSEC 1000000000
#define MAPLEN VIR_CPU_MAPLEN(maxCpu);
int numVcpu = 0;

typedef struct {
  virDomainPtr dom;
  unsigned long maxMem;
  unsigned long free;
  unsigned long total;
} DomainStats;

int* ListingDomain(virConnectPtr conn);
virDomainPtr* GetDomainArr(int numDomains, int* activeDomains, virConnectPtr conn);
DomainStats* GetDomainStats(virDomainPtr* domArr, int numDomains);
void PrintDomainStats(DomainStats* domStats, int numDomains, int maxcpus); 
double CalUsage(long long int, long long int);

void DestroyStats(DomainStats* d, int n) {
  if(d)    
    free(d);
}

unsigned long long GetNodeFreeMemory(virConnectPtr conn) {
  int nparams = 0;
  int free = -1;
  virNodeMemoryStatsPtr params = NULL;
  int cellNum = VIR_NODE_MEMORY_STATS_ALL_CELLS;
  if (virNodeGetMemoryStats(conn, cellNum, NULL, &nparams, 0) == 0 &&
      nparams != 0) {
    if ((params = malloc(sizeof(virNodeMemoryStats) * nparams)) == NULL)
      goto error;
    memset(params, 0, sizeof(virNodeMemoryStats) * nparams);
    if (virNodeGetMemoryStats(conn, cellNum, params, &nparams, 0))
      goto error;
  }
  if(params) {
    for(int i = 0; i < nparams; i++) {
      if(!strcmp(params[i].field, "free")) {
        free = params[i].value;
      }
    }
  }
  return free;
error:
  return -1;
}

int FindMax(DomainStats* d, int n, int except) {
  unsigned long r = 0;
  int max = -1;
  for(int i = 0; i < n; i++) {
    unsigned long u = d[i].free;
    if(u > r && i != except && u > 100 * 1024) {
      r = u;
      max = i;
    }
  }
  return max;
}


int main(int argc, char *argv[])
{
  virConnectPtr conn;

  conn = virConnectOpen("qemu:///system");
  if (conn == NULL) {
    fprintf(stderr, "Failed to open connection to qemu:///system\n");
    return 1;
  }
  
  int numDomains;
  int prevCounts = 0;
  int* activeDomains = NULL;
  virDomainPtr* domArr = NULL;
  DomainStats* curStats = NULL, *prevStats=NULL;
  double MIN_THRESHOLD = 20.0;
  double MAX_THRESHOLD = 60.0;


  if(argc < 2) {
    fprintf(stderr, "./[program] [interval]\n");
    return -1;
  }
  int interval = atoi(argv[1]);


  while((numDomains = virConnectNumOfDomains(conn)) > 0) {
    
    if(prevCounts != numDomains) {
      if(activeDomains)
        free(activeDomains);
      activeDomains = ListingDomain(conn);
      if(domArr)
        free(domArr);
      domArr = GetDomainArr(numDomains, activeDomains, conn);
      int time = interval / 2;
      if(time == 0)
        time = 1;
      for(int i = 0; i < numDomains; i++) 
        virDomainSetMemoryStatsPeriod(domArr[i], time, VIR_DOMAIN_AFFECT_LIVE | VIR_DOMAIN_AFFECT_CONFIG);
    }
    prevCounts = numDomains;
    curStats = GetDomainStats(domArr, numDomains);
    if(curStats == NULL) {
      fprintf(stderr, "curStats is NULL");
      return -1;
    }


    if(prevStats) {
      for(int i = 0; i < numDomains; i++) {
        unsigned long long total = curStats[i].total;
        unsigned long long free = curStats[i].free;
        double usage = CalUsage(free, total);
        unsigned long long xx, nodeFree = GetNodeFreeMemory(conn), alpha = prevStats[i].free - curStats[i].free, beta = curStats[i].free - prevStats[i].free;
        if(usage < MIN_THRESHOLD) {
          if(alpha < curStats[i].total / 10)
            alpha = curStats[i].total / 10;
          xx = curStats[i].total + alpha;
          if(xx > curStats[i].maxMem) {
            // error
          }
          else if(alpha > nodeFree) {
            // error
          }
          else {
            printf("Increase(%d) Memory (usage, before, after) : (%.2f%%, %llu, %llu)\n", i, usage, total, xx);
            printf("Node Free Memory : %llu\n\n", nodeFree);
            virDomainSetMemory(curStats[i].dom, xx);
            curStats[i].total += alpha;
            int max = FindMax(curStats, numDomains, i);
            if(max != -1) {
              double u = CalUsage(curStats[max].free, curStats[max].total);
              printf("u : %.2f%% \n", u);
              if(u > MIN_THRESHOLD + 11) {
                if(alpha > curStats[max].total / 10)
                  alpha = curStats[max].total / 10;
                xx = curStats[max].total - alpha;
                printf("Decrease(%d) Memory (usage, before, after) : (%.2f%%, %lu, %llu)\n", max, u, curStats[max].total, xx);
                printf("Node Free Memory : %llu\n\n", nodeFree);
                virDomainSetMemory(curStats[max].dom, xx);
                curStats[max].total -= alpha;
              }
            }
          }
        }  
        if(usage > MAX_THRESHOLD && free > 100 * 1024) {
          if(beta > curStats[i].total / 10)
            beta = curStats[i].total / 10;
          xx = curStats[i].total - beta;
          printf("Decrease(%d) Memory (usage, before, after) : (%.2f%%, %llu, %llu)\n", i, usage, total, xx);
          printf("Node Free Memory : %llu\n\n", nodeFree);
          virDomainSetMemory(curStats[i].dom, xx);
          curStats[i].total -= beta;
        }
      }
    }

    if(prevStats)
      DestroyStats(prevStats, numDomains);
    prevStats = curStats;

    /*
       PrintDomainStats(curStats, numDomains, maxCpu);
       */

    sleep(interval);
  }

  virConnectClose(conn);
  return 0;
}

int* ListingDomain(virConnectPtr conn)
{
  int i;
  int numDomains;
  int *activeDomains;

  numDomains = virConnectNumOfDomains(conn);

  activeDomains = calloc(numDomains, sizeof(int));
  numDomains = virConnectListDomains(conn, activeDomains, numDomains);

  printf("Active domain IDs:");
  for (i = 0 ; i < numDomains ; i++) {
        printf("  %d, ", activeDomains[i]);
  }
  printf("\n");
  return activeDomains;
}

virDomainPtr* GetDomainArr(int numDomains, int* activeDomains, virConnectPtr conn) 
{
  virDomainPtr * domArray = calloc(numDomains, sizeof(virDomainPtr));
  for(int i = 0; i < numDomains; i++) {
    domArray[i] = virDomainLookupByID(conn, activeDomains[i]);
  }

  return domArray;
}

DomainStats* GetDomainStats(virDomainPtr* domArr, int numDomains) 
{
  DomainStats* domStats = calloc(numDomains, sizeof(DomainStats));
  for(int i = 0; i < numDomains; i++) {
    virDomainPtr dom = domArr[i];
    domStats[i].dom = domArr[i];
    domStats[i].maxMem = virDomainGetMaxMemory(domStats[i].dom);

    int nr_stats = 10;
    virDomainMemoryStatPtr stats = calloc(nr_stats, sizeof(virDomainMemoryStatStruct));
    virDomainMemoryStats (dom, stats, nr_stats, 0);
    for(int j = 0; j < nr_stats; j++) {
      if(stats[j].tag == 6)
        domStats[i].total = stats[j].val;
      else if(stats[j].tag == 4)
        domStats[i].free = stats[j].val;
    }

  }
  return domStats;
}

/*
void PrintDomainStats(DomainStats* domStats, int numDomains, int maxcpus) 
{
  for(int i = 0; i < numDomains; i++) {
    printf("DomName(id) : %s(%d)\n", virDomainGetName(domStats[i].dom), i);
    printf("numCpus : %d\n", domStats[i].numCpus);
    virVcpuInfoPtr cpuInfo = domStats[i].cpuInfo;
    
    printf("=====  vCPU Info ======\n");
    for(int j = 0; j < domStats[i].numCpus; j++) {
      if(cpuInfo[j].state == 0)
        continue;
      printf("(vCpu ID, pCpuID, cpuTime) : (%d, %d, %.2f)\n", cpuInfo[j].number, cpuInfo[j].cpu, cpuInfo[j].cpuTime / (double)(NANOSEC));
      
      for (int k = 0; k < maxcpus; k++) {
        printf("%.2f|", VIR_CPU_USED(domStats[i].cpuMaps, k) ? domStats[i].usage[j][k] : 0.0);
      }
      printf("\n");
    }
    printf("=======================\n");

  }
}
*/
double CalUsage(long long int free, long long int total)
{
  return ((double)free / (double) total) * 100;
}

