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
#define THRESHOLD 15.0
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

double absol(double x) {
  if(x > 0)
    return x;
  else
    return -x;
}

int FindMin(double* pcpuUsageArr, double avg, int maxCpu, int zero) {
  double max = 0;
  int m = -1;
  for(int i = 0; i < maxCpu; i++) {
    if(zero) {
      if(pcpuUsageArr[i] == 0) {
        m = i;
        break;
      }
    }
    else if(pcpuUsageArr[i] != 0) {
      double ab = absol(pcpuUsageArr[i] - avg);
      if(ab > max && ab > THRESHOLD) {
        max = ab;
        m = i;
      }
    }
  }
  return m;
}

double GetSum(double* pArr, int maxCpu) {
  double sum = 0;
  for(int i = 0; i < maxCpu; i++) {
    sum += pArr[i];
  }
  return sum;
}

double GetTrueSum(double *pArr, int maxCpu, int* pnum) {
  double sum = 0;
  for(int i = 0; i < maxCpu; i++) {
    sum += pArr[i] * pnum[i];
  }
  return sum;
}


double GetAvg(double* pArr, int maxCpu, int used_cpu) {
  double sum = GetSum(pArr, maxCpu);
  double avg = sum / used_cpu;
  return avg;
}

int* Find_ij(DomainStats* curDomain, double* pArr, int numDomains, int maxCpu, int min, int used_pcpu, int zero, int *pcpuNum) {
  int * result = calloc(3, sizeof(int));
  //  if(!zero_b) {
  double avg = GetAvg(pArr, maxCpu, used_pcpu);
  double x = avg - pArr[min];
  double mmin = 0, mmax = 0;
  int m = -1, i_min = -1, j_min = -1, k_min = -1;


  for(int i = 0; i < maxCpu; i++) {
    if(x > 0) {
      if(mmin <= pArr[i] - avg) {
        mmin = pArr[i] - avg;
        m = i;
      }
    }
    else {
      if(mmax <= avg - pArr[i] && (zero || pArr[i] != 0)) {
        mmax = avg - pArr[i];
        m = i;
      }
    }
  }

  mmin = LLONG_MAX;

  for(int i = 0; i < numDomains; i++) {
    for(int j = 0; j < curDomain[i].numCpus; j++) {
      if(curDomain[i].usage[j][m] != 0 || zero) {
        double ab = absol(curDomain[i].usage[j][m] + pArr[min] - avg);
        if(ab < mmin) {
          mmin = ab;
          i_min = i;
          j_min = j;
        }
      }
    }
  } 

  result[0] = i_min;
  result[1] = j_min;
  result[2] = m;
  return result;
}

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
      if(cpuStats[i].usage[j][max] < 70 && cpuStats[i].usage[j][max] != 0) {
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
          int tmp = j * maxCpu + (index % maxCpu);
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
  double period = 0; 
  struct timeval curtime, prevtime;


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
    gettimeofday(&curtime, 0);
    //curtime = time(0);
    period = curtime.tv_sec - prevtime.tv_sec + (curtime.tv_usec - prevtime.tv_usec)/1000000;
    prevtime = curtime;
    //if(curStats)
    //DestroyStats(curStats, numDomains);
    curStats = GetDomainStats(domArr, numDomains, maxCpu);
    if(prevStats) {
      double* pcpuUsageArr =  NULL;
      pcpuUsageArr = calloc(maxCpu, sizeof(double));
      int* pcpuNum = CalculateStats(curStats, prevStats, (double)period, pcpuUsageArr, numDomains, maxCpu);
      if(CheckAffinity(curStats, numDomains, maxCpu)) {
        curStats = GetDomainStats(domArr, numDomains, maxCpu);
        if(pcpuNum)
          free(pcpuNum);
        pcpuNum = CalculateStats(curStats, prevStats, (double)period, pcpuUsageArr, numDomains, maxCpu);
      }
      PrintDomainStats(curStats, numDomains, maxCpu);
      PinCPUs(curStats, pcpuUsageArr, maxCpu, pcpuNum, numDomains, domArr);
      if(pcpuUsageArr)
        free(pcpuUsageArr);
      if(pcpuNum)
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

  int* pcpuNum = NULL;
  pcpuNum = calloc(maxCpu, sizeof(int));

  for(int i = 0; i < maxCpu; i++) { 
    pcpuNum[i] = 0;
    pcpuUsageArr[i] = 0.0;
  }
  numVcpu = 0;

  for(int i = 0; i < numDomains; i++) {
    for(int j = 0; j < curStats[i].numCpus; j++) {
      if(curStats[i].cpuInfo[j].state != 0) {
        for(int k = 0; k < maxCpu; k++) {
          if(checkMap(curStats[i].cpuMaps, k, j, maxCpu)) {
            pcpuNum[k] += 1;
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
          curStats[i].usage[j][k] = CalUsage(curStats[i].cpuInfo[j].cpuTime - prevStats[i].cpuInfo[j].cpuTime, period * GetNumCPU(curStats[i].cpuMaps, maxCpu));
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
    //printf("(pcpu id, usage) : %d, %.2f%%\n", i, pcpuUsageArr[i]);
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
  int min, max, zero, zero_b, min_real;
  double min_u, max_u;
  int count = 0;
  double sum = GetTrueSum(pcpuUsageArr, maxCpu, pcpuNum);
  while(1) {
    int used_pcpu = 0;
    for(int i = 0 ; i < maxCpu; i++) {
      if(pcpuNum[i] != 0) {
        used_pcpu += 1;
      }
    }
    double avg = GetAvg(pcpuUsageArr, maxCpu, used_pcpu);
    int zero = (sum > maxCpu * 99) && (used_pcpu != numVcpu);
    printf("avg : %f\n", avg);
    printf("sum :%f\n", sum);
    int min = FindMin(pcpuUsageArr, avg, maxCpu, zero);

    printf("%f, %d, %d, %d\n", pcpuUsageArr[min], min, used_pcpu, zero);
    if(min == -1)
      break;

    if(count == maxCpu * maxCpu)
      break;

    // FIND J
    // add near avg
    int* ij = Find_ij( curStats, pcpuUsageArr , numDomains, maxCpu, min, used_pcpu, zero, pcpuNum);
    if(ij == NULL)
      fprintf(stderr, "Find_ij problem");
    int i = ij[0];
    int j = ij[1];
    int k = ij[2];
    printf("i, j, k, value : %d, %d, %d, %f\n", i, j, k, curStats[i].usage[j][k]);
    if(i == -1 || j == -1 || k == -1) {
      exit(-1);
    }
    int index;
    cpumap = calloc(curStats[i].numCpus, VIR_CPU_MAPLEN(maxCpu));
    if(avg - pcpuUsageArr[min] > 0) {
      index = j * maxCpu + min;
    }
    else {
      index = j * maxCpu + k;
      if(ij)
        free(ij);
      ij = Find_ij(curStats, pcpuUsageArr, numDomains, maxCpu, k, used_pcpu, zero, pcpuNum);
      i = ij[0];
      j = ij[1];
      k = ij[2];
    }
    cpumap[index / 8] = 1 << (index % 8);
    index = index % maxCpu;
    pcpuUsageArr[k] -= (curStats[i].usage[j][k]);
    pcpuUsageArr[index] += (curStats[i].usage[j][k]);
    curStats[i].usage[j][index] += (curStats[i].usage[j][k]);
    curStats[i].usage[j][k] -= (curStats[i].usage[j][k]);
    pcpuNum[k] -= 1;
    pcpuNum[index] += 1;
    curStats[i].cpuInfo[j].cpu = index;
    virDomainPinVcpu(curStats[i].dom, curStats[i].cpuInfo[j].number, cpumap, VIR_CPU_MAPLEN(maxCpu));

    free(cpumap);
    free(ij);
    count++;

  }
}
