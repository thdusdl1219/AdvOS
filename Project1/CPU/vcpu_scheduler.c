/* example ex1.c */
/* compile with: gcc -g -Wall ex1.c -o ex -lvirt */
#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <limits.h>



typedef struct {
  long long int pcpu;
  long long int vcpu;
} PcpuTime;

typedef struct {
  virDomainPtr dom;
  int numCpus;
  virVcpuInfoPtr cpuInfo;
  unsigned char* cpuMaps;
  double* usage;
  PcpuTime* time;
} DomainStats;

typedef struct {
  long long int kernel;
  long long int user;
  long long int idle;
  long long int iowait;
} NodeTime;

#define NANOSEC 1000000000
#define THRESHOLD 50.0
int numVcpu = 0;

int* ListingDomain(virConnectPtr conn);
virDomainPtr* GetDomainArr(int numDomains, int* activeDomains, virConnectPtr conn);
DomainStats* GetDomainStats(virDomainPtr* domArr, int numDomains, int maxCpu);
void PrintDomainStats(DomainStats* domStats, int numDomains, int maxcpus); 
NodeTime* GetNodeTimes(virConnectPtr conn, int maxCpu); 
int* CalculateStats(DomainStats* curStats, DomainStats* prevStats, double period, double* pcpuUsageArr, int numDomains, int); 
double CalUsage(long long int, long long int);
void PinCPUs(DomainStats*, double*, int, int*, int, virDomainPtr*);

int GetNumCPU(unsigned char* cpumap, int maxCpu) {
  int result = 0;
  for(int i = 0; i < maxCpu; i++) {
    if(VIR_CPU_USED(cpumap, i)) 
      result += 1;
  }
  return result;
}

int CheckAffinity(DomainStats *curStats, int numDomains, int maxCpu) {
  int result = 0;
  for(int i = 0; i < numDomains ; i++) {
    int t = (GetNumCPU(curStats[i].cpuMaps, maxCpu) > (numDomains / maxCpu) + 1);
    unsigned char *cpumap = calloc(curStats[i].numCpus, VIR_CPU_MAPLEN(maxCpu));
    result |= t;
    if(t) {
      for(int j = 0; j < curStats[i].numCpus; j++) {
        if(curStats[i].cpuInfo[j].state != 0) {
          static int index = 0;
          cpumap[index / maxCpu] = 1 << (index % maxCpu);
          virDomainPinVcpu(curStats[i].dom, curStats[i].cpuInfo[j].number, cpumap, VIR_CPU_MAPLEN(maxCpu)); 
          index++;

        }
      }
    }
    free(cpumap);
  }
  return result;
}

int main(int argc, char *argv[])
{
  virConnectPtr conn;

  conn = virConnectOpen("qemu:///system");
  if (conn == NULL) {
    fprintf(stderr, "Failed to open connection to qemu:///system\n");
    return 1;
  }
  
  int maxCpu = virNodeGetCPUMap(conn, NULL, NULL, 0);
  int numDomains;
  int prevCounts = 0;
  int* activeDomains;
  virDomainPtr* domArr;
  DomainStats* curStats, *prevStats = NULL;

  /*
  NodeTime* nodeTime = GetNodeTimes(conn, maxCpu);
  if(nodeTime == NULL) {
    fprintf(stderr, "Failed to get NodeTime\n");
    return 1;
  }
  */

  if(argc < 2) {
    fprintf(stderr, "./[program] [interval]\n");
    return -1;
  }
  int interval = atoi(argv[1]);


  while((numDomains = virConnectNumOfDomains(conn)) > 0) {
    if(prevCounts != numDomains) {
      activeDomains = ListingDomain(conn);
      domArr = GetDomainArr(numDomains, activeDomains, conn);
    }
    prevCounts = numDomains;

    curStats = GetDomainStats(domArr, numDomains, maxCpu);
    //gettimeofday(&cur, NULL);
    if(prevStats) {
      double* pcpuUsageArr = calloc(maxCpu, sizeof(double));
      int* pcpuNum = CalculateStats(curStats, prevStats, (double)interval, pcpuUsageArr, numDomains, maxCpu);
      PrintDomainStats(curStats, numDomains, maxCpu);
      if(CheckAffinity(curStats, numDomains, maxCpu)) {
        curStats = GetDomainStats(domArr, numDomains, maxCpu);
        pcpuNum = CalculateStats(curStats, prevStats, (double)interval, pcpuUsageArr, numDomains, maxCpu);
      }
      PinCPUs(curStats, pcpuUsageArr, maxCpu, pcpuNum, numDomains, domArr);
    }

    
    prevStats = curStats;
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

DomainStats* GetDomainStats(virDomainPtr* domArr, int numDomains, int maxCpu) 
{
  virVcpuInfoPtr cpuInfo;
  int cpulen;
  unsigned char* cpuMaps;
  DomainStats* domStats = calloc(numDomains, sizeof(DomainStats));
  for(int i = 0; i < numDomains; i++) {
    domStats[i].dom = domArr[i];
    domStats[i].numCpus = virDomainGetCPUStats(domArr[i], NULL, 0, 0, 0, 0);

    cpuInfo = calloc(domStats[i].numCpus, sizeof(virVcpuInfo));
    cpulen = VIR_CPU_MAPLEN(maxCpu);
    cpuMaps = calloc(domStats[i].numCpus, cpulen);

    virDomainGetVcpus(domArr[i], cpuInfo, domStats[i].numCpus, cpuMaps, cpulen);
    domStats[i].cpuInfo = cpuInfo;
    printf("domain(%d) time : %lld", i, domStats[i].cpuInfo[0].cpuTime);
    domStats[i].cpuMaps = cpuMaps;
    int nparams = virDomainGetCPUStats(domArr[i], NULL, 0, 0, 1, 0); // nparams
    virTypedParameterPtr params = calloc(maxCpu * nparams, sizeof(virTypedParameter));
    virDomainGetCPUStats(domArr[i], params, nparams, 0, maxCpu, 0); // per-cpu stats
    domStats[i].time = calloc(maxCpu, sizeof(PcpuTime));
    for(int j = 0; j < maxCpu; j++) {
      domStats[i].time[j].pcpu = *(long long int *)(&params[j * 2].value);
      domStats[i].time[j].vcpu = *(long long int*)(&params[j * 2 + 1].value);
    }

  }

  return domStats;
}

void PrintDomainStats(DomainStats* domStats, int numDomains, int maxcpus) 
{
  for(int i = 0; i < numDomains; i++) {
    printf("DomName(id) : %s(%d)\n", virDomainGetName(domStats[i].dom), i);
    printf("numCpus : %d\n", domStats[i].numCpus);
    virVcpuInfoPtr cpuInfo = domStats[i].cpuInfo;
    double *usage = domStats[i].usage;
    
    printf("=====  vCPU Info ======\n");
    for(int j = 0; j < domStats[i].numCpus; j++) {
      if(cpuInfo[j].state == 0)
        continue;
      printf("(vCpu ID, pCpuID, cpuTime, usage) : (%d, %d, %.2f, %.2f%%)\n", cpuInfo[j].number, cpuInfo[j].cpu, cpuInfo[j].cpuTime / (double)(NANOSEC), usage[j]);

      
      for (int k = 0; k < maxcpus; k++) {
        printf("%c", VIR_CPU_USED(domStats[i].cpuMaps, k) ? '1' : '0');
      }
      printf("\n");
    }
    printf("=======================\n");

  }
}

NodeTime* GetNodeTimes(virConnectPtr conn, int maxCpu) 
{
  NodeTime* nodeTime = calloc(maxCpu, sizeof(NodeTime));
  virNodeCPUStatsPtr param;
  for(int i = 0; i < maxCpu; i++) {
    int nparams = 0;
    if(virNodeGetCPUStats(conn, i, NULL, &nparams, 0) == 0 && nparams != 0) {
      if((param = calloc(nparams, sizeof(virNodeCPUStats))) == NULL)
        return NULL;
      memset(param, 0, sizeof(virNodeCPUStats) * nparams);
      if(virNodeGetCPUStats(conn, i, param, &nparams, 0))
        return NULL;
    }
    nodeTime[i].kernel = param[0].value;
    nodeTime[i].user = param[1].value;
    nodeTime[i].idle = param[2].value;
    nodeTime[i].iowait = param[3].value;
  }
  return nodeTime;
}

int* CalculateStats(DomainStats* curStats, DomainStats* prevStats, double period, double* pcpuUsageArr, int numDomains, int maxCpu)
{

  int* pcpuNum = calloc(maxCpu, sizeof(int));

  for(int i = 0; i < maxCpu; i++) { 
    pcpuNum[i] = 0.0;
    pcpuUsageArr[i] = 0.0;
  }
  numVcpu = 0;
  for(int i = 0; i < numDomains; i++) {
    curStats[i].usage = calloc(curStats[i].numCpus, sizeof(double));
    for(int j = 0; j < curStats[i].numCpus; j++) {
      if(curStats[i].cpuInfo[j].state == 0)
        continue;
      curStats[i].usage[j] = CalUsage(curStats[i].cpuInfo[j].cpuTime - prevStats[i].cpuInfo[j].cpuTime, period);
      for(int k = 0; k < maxCpu; k++) {
        if(curStats[i].cpuMaps[k / 8] & (1 << k % 8)) {
          pcpuUsageArr[k] += curStats[i].usage[j] / GetNumCPU(curStats[i].cpuMaps, maxCpu);
          pcpuNum[k] += 1.0;
        }
      }
      numVcpu += 1;
    }
  }

  for(int i = 0; i < maxCpu; i++) {
    if(pcpuNum[i] == 0)
      continue;
    //pcpuUsageArr[i] = (pcpuUsageArr[i] / (double)period) * 100;
    printf("(pcpu id, usage) : %d, %.2f%%\n", i, pcpuUsageArr[i]);
    //PrintDomainStats(curStats, numDomains, maxCpu);
    //pcpuUsageArr[i] = pcpuUsageArr[i] * (double)pcpuNum[i]; 
  }
  return pcpuNum;
}

double CalUsage(long long int diff, long long int period)
{
  return (((double)(diff)/NANOSEC) / (double) period) * 100;
}

void PinCPUs(DomainStats* curStats, double* pcpuUsageArr, int maxCpu, int* pcpuNum, int numDomains, virDomainPtr* domArr)  
{
  unsigned char* cpumap;
  int min, max, zero, zero_b;
  double min_u, max_u;
  //DomainStats* tmpStats;
  //double interval;
  while(1) {

restart:
/*    tmpStats = GetDomainStats(domArr, numDomains, maxCpu);
    struct timeval now;
    gettimeofday(&now, NULL);

    interval = (double)(((now.tv_sec - cur.tv_sec) * 1000000) + (now.tv_usec - cur.tv_usec)) / 1000000;
    cur = now;
    printf("%f\n", interval);
    pcpuNum = CalculateStats(tmpStats, curStats, interval, pcpuUsageArr, numDomains, maxCpu);
    curStats = tmpStats;
*/

    min = -1, max = -1, zero = -1;
    zero_b = 0;
    min_u = LLONG_MAX, max_u = 0;
    int used_pcpu = 0;
    
    for(int i = maxCpu - 1; i >= 0; i--) {
      if(pcpuUsageArr[i] > 0) {
        if(pcpuUsageArr[i] > max_u) {
          max_u = pcpuUsageArr[i];
          max = i;
        }
        if(pcpuUsageArr[i] < min_u) {
          min_u = pcpuUsageArr[i];
          min = i;
        }
        used_pcpu++;
      }
      else {
        zero_b |= 1;
        zero = i;
      }
    }

    printf("%f, %f, %d, %d, %d, %d, %d\n", max_u, min_u, max, min, zero, used_pcpu, numVcpu);
    if(max_u - min_u < THRESHOLD && ((zero_b && numVcpu == used_pcpu) || !zero_b))
      break;
    

    for(int i = 0; i < numDomains; i++) {
      for(int j = 0; j < curStats[i].numCpus; j++) {
        if(curStats[i].cpuInfo[j].cpu == max && curStats[i].cpuInfo[j].state != 0) {
          cpumap = calloc(curStats[i].numCpus, VIR_CPU_MAPLEN(maxCpu));
          int index;
          if(zero_b) {
            memset(cpumap, 0, VIR_CPU_MAPLEN(maxCpu));
            index = zero;
            cpumap[index / 8] = 1 << (index % 8);
          }
          else {
            index = min;
            cpumap[index / 8] |= 1 << (index % 8);
          }
          virDomainPinVcpu(curStats[i].dom, curStats[i].cpuInfo[j].number, cpumap, VIR_CPU_MAPLEN(maxCpu));
          free(cpumap);
          pcpuUsageArr[max] -= (*curStats[i].usage / GetNumCPU(curStats[i].cpuMaps, maxCpu));
          pcpuUsageArr[index] += (*curStats[i].usage / GetNumCPU(curStats[i].cpuMaps, maxCpu));
          pcpuNum[curStats[i].cpuInfo[j].cpu] -= 1;
          pcpuNum[index] += 1;
          curStats[i].cpuInfo[j].cpu = index;
          goto restart;
        }
      }
    }



  }
}
