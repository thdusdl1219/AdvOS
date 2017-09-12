/* example ex1.c */
/* compile with: gcc -g -Wall ex1.c -o ex -lvirt */
#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <string.h>

int* ListingDomain(virConnectPtr conn);

int main(int argc, char *argv[])
{
  virConnectPtr conn;

  conn = virConnectOpen("qemu:///system");
  if (conn == NULL) {
    fprintf(stderr, "Failed to open connection to qemu:///system\n");
    return 1;
  }


  int* activeDomains = ListingDomain(conn);
  int numDomains = virConnectNumOfDomains(conn);

  virDomainPtr dom;

  dom = virDomainLookupByID(conn, activeDomains[0]);

  int nparams = virDomainGetCPUStats(dom, NULL, 0, -1, 1, 0); // nparams
  printf("nparams : %d\n", nparams);
  virTypedParameterPtr params = calloc(nparams, sizeof(virTypedParameter));
  int tstat = virDomainGetCPUStats(dom, params, nparams, -1, 1, 0); // total stats.
  printf("total stat : %d\n", tstat);

  /*
  int ncpus = virDomainGetCPUStats(dom, NULL, 0, 0, 0, 0); // ncpus
  printf("ncpus : %d\n", ncpus);
  int nparams = virDomainGetCPUStats(dom, NULL, 0, 0, 1, 0); // nparams
  printf("nparams : %d\n", nparams);

  virTypedParameterPtr params = calloc(ncpus * nparams, sizeof(virTypedParameter));
  int stat = virDomainGetCPUStats(dom, params, nparams, 0, ncpus, 0); // per-cpu stats
  printf("stat : %d\n", stat);
*/
  printf("%s\n", params[0].field);
  printf("%llu\n", params[0].value);
  printf("%s\n", params[1].field);
  printf("%llu\n", params[1].value);
  printf("%s\n", params[2].field);
  printf("%llu\n", params[2].value);
  int cpuNum = VIR_NODE_CPU_STATS_ALL_CPUS;
  nparams = 0;
  virNodeCPUStatsPtr param;
  if (virNodeGetCPUStats(conn, cpuNum, NULL, &nparams, 0) == 0 && nparams != 0) {
    printf("nparams : %d\n", nparams);
    if ((param = malloc(sizeof(virNodeCPUStats) * nparams)) == NULL)
      return -1;
    memset(params, 0, sizeof(virNodeCPUStats) * nparams);
    if (virNodeGetCPUStats(conn, cpuNum, param, &nparams, 0))
      return -1;
  }
  printf("%s\n", param[0].field);
  printf("%llu\n", param[0].value);
  printf("%s\n", param[1].field);
  printf("%llu\n", param[1].value);
  printf("%s\n", param[2].field);
  printf("%llu\n", param[2].value);
  printf("%s\n", param[3].field);
  printf("%llu\n", param[3].value);
  


  virConnectClose(conn);
  return 0;
}

int* ListingDomain(virConnectPtr conn)
{
  int i;
  int numDomains;
  int *activeDomains;

  numDomains = virConnectNumOfDomains(conn);

  activeDomains = malloc(sizeof(int) * numDomains);
  numDomains = virConnectListDomains(conn, activeDomains, numDomains);

  printf("Active domain IDs:\n");
  for (i = 0 ; i < numDomains ; i++) {
        printf("  %d\n", activeDomains[i]);
  }
  return activeDomains;
}
