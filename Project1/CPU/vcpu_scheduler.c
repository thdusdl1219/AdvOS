/* example ex1.c */
/* compile with: gcc -g -Wall ex1.c -o ex -lvirt */
#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
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
  double** usage;
  PcpuTime* time;
} DomainStats;

typedef struct {
  long long int kernel;
  long long int user;
  long long int idle;
  long long int iowait;
} NodeTime;

#define NANOSEC 1000000000
#define THRESHOLD 30.0
#define MAPLEN VIR_CPU_MAPLEN(maxCpu);
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

int checkMap(unsigned char* cpumap, int pcpu, int vcpu, int maxCpu) {
  int index = vcpu * maxCpu + pcpu;
  return cpumap[index / 8] & (1 << index % 8);
}

int check(DomainStats *cpuStats, int max, int numDomains, int maxCpu, int* pcpuNum) {
  int result = 0;
  for(int i = 0; i < numDomains; i++) {
    for(int j = 0; j < cpuStats[i].numCpus; j++) {
      if(cpuStats[i].usage[j][max] < 90 && cpuStats[i].usage[j][max] != 0) {
        result |= 1;
      }
    }
  }
  return result;
}

void DestroyStats(DomainStats* d, int n) {
  for(int i = 0; i < n; i++) {
    if(d[i].cpuInfo)    
      free(d[i].cpuInfo);
    if(d[i].cpuMaps)    
      free(d[i].cpuMaps);
    if(d[i].time)    
      free(d[i].time);
    if(d[i].usage) { 
      for(int j = 0; j < d[i].numCpus; j++) {
        if(d[i].usage[j])    
          free(d[i].usage[j]);
        }
      free(d[i].usage);
      }
  }
  if(d)    
    free(d);
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
          int tmp = j * maxCpu + index;
          cpumap[tmp/ 8] = 1 << (tmp % 8);
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
  int* activeDomains = NULL;
  virDomainPtr* domArr = NULL;
  DomainStats* curStats = NULL, *prevStats = NULL;

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
  int period = 0, curtime = 0, prevtime = 0;


  while((numDomains = virConnectNumOfDomains(conn)) > 0) {
    if(prevCounts != numDomains) {
      if(activeDomains)
        free(activeDomains);
      activeDomains = ListingDomain(conn);
      if(domArr)
        free(domArr);
      domArr = GetDomainArr(numDomains, activeDomains, conn);
    }
    prevCounts = numDomains;
    curtime = time(0);
    period = curtime - prevtime;
    prevtime = curtime;
    //if(curStats)
      //DestroyStats(curStats, numDomains);
    curStats = GetDomainStats(domArr, numDomains, maxCpu);
    if(prevStats) {
      double* pcpuUsageArr = calloc(maxCpu, sizeof(double));
      int* pcpuNum = CalculateStats(curStats, prevStats, (double)period, pcpuUsageArr, numDomains, maxCpu);
      PrintDomainStats(curStats, numDomains, maxCpu);
      if(CheckAffinity(curStats, numDomains, maxCpu)) {
        curStats = GetDomainStats(domArr, numDomains, maxCpu);
        if(pcpuNum)
          free(pcpuNum);
        pcpuNum = CalculateStats(curStats, prevStats, (double)period, pcpuUsageArr, numDomains, maxCpu);
      }
      PinCPUs(curStats, pcpuUsageArr, maxCpu, pcpuNum, numDomains, domArr);
      free(pcpuUsageArr);
      free(pcpuNum);
    }

    if(prevStats)
      DestroyStats(prevStats, numDomains);
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
    printf("nparams = %d\n", nparams);
    virDomainGetCPUStats(domArr[i], params, nparams, 0, maxCpu, 0); // per-cpu stats
    domStats[i].time = calloc(maxCpu, sizeof(PcpuTime));
    for(int j = 0; j < maxCpu; j++) {
      domStats[i].time[j].pcpu = *(long long int *)(&params[j * 2].value);
      domStats[i].time[j].vcpu = *(long long int*)(&params[j * 2 + 1].value);
      if(j == 0 || j == 1) {
        printf("pcpu(%d) time : %lld", j, domStats[i].time[j].pcpu);
        printf("vcpu(%d) time : %lld", j, domStats[i].time[j].vcpu);
      }

    }
    free(params);

  }

  return domStats;
}

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
    for(int j = 0; j < curStats[i].numCpus; j++) {
      if(curStats[i].cpuInfo[j].state != 0) {
        for(int k = 0; k < maxCpu; k++) {
          if(checkMap(curStats[i].cpuMaps, k, j, maxCpu)) {
            pcpuNum[k] += 1.0;
          }
        }
      } 
    }
  }
  for(int i = 0; i < numDomains; i++) {
    curStats[i].usage = calloc(curStats[i].numCpus, sizeof(double*));
    for(int j = 0; j < curStats[i].numCpus; j++) {
      curStats[i].usage[j] = calloc(maxCpu, sizeof(double));
      if(curStats[i].cpuInfo[j].state == 0)
        continue;
      for(int k = 0; k < maxCpu; k++) {
        if(checkMap(curStats[i].cpuMaps, k, j, maxCpu)) {
          curStats[i].usage[j][k] = CalUsage(curStats[i].cpuInfo[j].cpuTime - prevStats[i].cpuInfo[j].cpuTime, period * GetNumCPU(curStats[i].cpuMaps, maxCpu)) * pcpuNum[k];
          pcpuUsageArr[k] += curStats[i].usage[j][k];
          //pcpuNum[k] += 1.0;
        }
      }
      numVcpu += 1;
    }
  }

  for(int i = 0; i < maxCpu; i++) {
    if(pcpuNum[i] == 0)
      continue;
    //pcpuUsageArr[i] = (pcpuUsageArr[i] / (double)period) * 100;
    printf("(pcpu id, usage) : %d, %.2f%%\n", i, pcpuUsageArr[i] / pcpuNum[i]);
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
  int count = 0;
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
    count++;
    
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

    int c = check(curStats, max, numDomains, maxCpu, pcpuNum);
    printf("%f, %f, %d, %d, %d, %d, %d\n", max_u, min_u, max, min, zero, used_pcpu, c);
    if(max_u - min_u < THRESHOLD * pcpuNum[max] && (max_u < 120 || c))
      break;
    if(count == maxCpu * maxCpu)
      break;
    

    for(int i = 0; i < numDomains; i++) {
      for(int j = 0; j < curStats[i].numCpus; j++) {
        if(curStats[i].cpuInfo[j].cpu == max && curStats[i].cpuInfo[j].state != 0) {
          cpumap = calloc(curStats[i].numCpus, VIR_CPU_MAPLEN(maxCpu));
          int index;
          if(zero_b && min_u > 50) {
            memset(cpumap, 0, VIR_CPU_MAPLEN(maxCpu));
            index = j * maxCpu + zero;
            cpumap[index / 8] = 1 << (index % 8);
            pcpuUsageArr[max] -= (curStats[i].usage[j][max]);
            pcpuUsageArr[index] += (curStats[i].usage[j][max]);
            curStats[i].usage[j][index] += curStats[i].usage[j][max];
            curStats[i].usage[j][max] = 0;
            pcpuNum[curStats[i].cpuInfo[j].cpu] -= 1;
            pcpuNum[index] += 1;
            curStats[i].cpuInfo[j].cpu = index;
          }
          else if(zero_b) {
            index = j * maxCpu + min;
            cpumap[index / 8] = 1 << (index % 8);
            pcpuUsageArr[max] = 0;
            pcpuUsageArr[index] = 0;
            curStats[i].usage[j][index] += curStats[i].usage[j][max];
            curStats[i].usage[j][max] = 0;
            pcpuNum[curStats[i].cpuInfo[j].cpu] -= 1;
            pcpuNum[index] += 1;
            curStats[i].cpuInfo[j].cpu = index;
          }
          else {
            index = j * maxCpu + min;
            cpumap[index / 8] |= 1 << (index % 8);
            pcpuUsageArr[max] -= (curStats[i].usage[j][max] / (pcpuNum[max] + 1));
            pcpuUsageArr[index] += (curStats[i].usage[j][max] / (pcpuNum[max] + 1));
            curStats[i].usage[j][index] += (curStats[i].usage[j][max] / (pcpuNum[max] + 1));
            curStats[i].usage[j][max] -= (curStats[i].usage[j][max] / (pcpuNum[max] + 1));
            //pcpuNum[max] -= 1;
            pcpuNum[index] += 1;
            curStats[i].cpuInfo[j].cpu = index;
          }
          virDomainPinVcpu(curStats[i].dom, curStats[i].cpuInfo[j].number, cpumap, VIR_CPU_MAPLEN(maxCpu));
          free(cpumap);

          goto restart;
        }
      }
    }
  }
}
