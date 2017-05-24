#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define PAGEMAP_LENGTH 8
#define PAGE_SHIFT 12

/*
	O programa consiste em acessar o mapeamento de endereço lógico para 
	físico das páginas de dois processos: pai e filho, e constatar que
	esses processos tem o mesmo endereço lógico de uma variável que é
	mapeado para dois endereços físicos diferentes. A segunda hipótese
	é a de que com endereços lógicos diferentes nos processos pai e filho
	é possíveo referenciar um mesmo endereço físico de um espaço de memória
	compartilhada. 

	Como já visto anteriormente na disciplina, um processo filho herda
	todo o espaço de endereçamento do pai, por isso a constatação desse
	programa será possível.

	O programa aqui descrito é baseado na referência:

	https://shanetully.com/2014/12/translating-virtual-addresses-to-physcial-addresses-in-user-space/  
	
	A primeira observação sobre o programa é:

	O arquivo /proc/[PID]/pagemap dá acesso pelo espaço de usuário
	a como o kernel gerencia as páginas no sistema. Mais especifica-
	mente, permite a um processo no espaço de usuário saber a qual 
	quadro de página cada página do processo está mapeada.
*/


/*
	Função que retorna o endereço do quadro em que está contido um endereço lógico
*/
unsigned long get_page_frame_number_of_address(void *addr) {
   
   // Construindo o caminho para o arquivo pagemap a partir do PID do processo.
   char path_to_pagemap[100];
   char PID[50];
   sprintf(PID,"%d",getpid());
   strcpy(path_to_pagemap,"/proc/");
   strcat(path_to_pagemap,PID);
   strcat(path_to_pagemap,"/pagemap");
   
   // Abrindo o arquivo citado na descrição do mapeamento de página -> quadro de página
   FILE *pagemap = fopen(path_to_pagemap, "rb");
   if(pagemap == NULL) printf("fail to open pagemap file\n");
   /*
   		Para descobrir o número do quadro de uma página a partir 
   		de um endereço lógico são necessários alguns, dentre eles
   		o cálculo do offset de deslocamento dentro do arquivo pagemap
   		para obter as informações do quadro desejado.

   		Primeiramente, este offset será usado para descobrir o número
   		do quadro buscado.

   		A primeira parte do cálculo é a fórmula:

   		(unsigned long)addr / getpagesize()

   		Esta fórmula indica qual o número da página em que o endereço
   		(addr) está contido. Como a memória virtual é endereçada se-
   		quencialmente, se dividirmos um endereço pelo tamanho da pági-
   		na obteremos em qual bloco de 4096KB o endereço dado está 
   		contido. 

   		* PAGEMAP_LENGTH;

   		O arquivo pagemap tem seu conteúdo dividido em blocos de 64 bits
   		cada qual referenciando a um quadro de página na memória. Para que
   		haja o acesso ao quadro de página buscada pelo endereço lógico, é
   		necessário deslocar o ponteiro do arquivo dentro desses blocos de 
   		64 bits ou 8 bytes cada, portanto o acesso a 2ª página por exemplo
   		poderia ser dado com o endereço 0x1000 (4096)/ 4096 (page size) *
   		8 bytes (deslocamento). Logo, o ponteiro para leitura pararia exa-
   		tamente na 2º página.

   		Divisão dos bits do conteúdo de um bloco do arquivo pagemap:
   		
   		Observe que o arquivo contém nos seus primeiros 55 bits o número
   		do quadro de página, como diz a documentação do kernel:
		
		* Bits 0-54  page frame number (PFN) if present
	    * Bit  55    pte is soft-dirty (see Documentation/vm/soft-dirty.txt)
	    * Bit  56    page exclusively mapped (since 4.2)
	    * Bits 57-60 zero
	    * Bit  61    page is file-page or shared-anon (since 3.5)
	    * Bit  62    page swapped
	    * Bit  63    page present

		https://www.kernel.org/doc/Documentation/vm/pagemap.txt	 
   */
   unsigned long offset = (unsigned long)addr / getpagesize() * PAGEMAP_LENGTH;
   	
   /*Andando com o ponteiro para leitura posterior no arquivo pagemap
   de acordo com offset calculado.*/
   if(fseek(pagemap, (unsigned long)offset, SEEK_SET) != 0) {
      fprintf(stderr, "Failed to seek pagemap to proper location\n");
      exit(1);
   }


   unsigned long page_frame_number = 0;//tamanho de 8 bytes
   /*
   	O número do quadro de página está nos primeiros 55 bits, como descrito anteriormente.
   	Logo lemos 7 bytes = 56 bits e tiramos do resultado o último bit.
   */

   //Lendo os 56 bits descritos.
   fread(&page_frame_number, 1, PAGEMAP_LENGTH-1, pagemap);
   /* 	Operação para retirada do 56º bit:

   		Isto é feito com uma operação AND sobre os bits recebidos
   		com o número em hexa 7FFFFFFFFFFFFF que em binário equiva-
   		le aos 4 primeiros bits igual a 0111 e o resto dos bits 
   		igual 1. O resultado da operação é portanto todos os bits
   		iguais exceto o 55º, que foi zerado.
   */
   page_frame_number &= 0x7FFFFFFFFFFFFF;

   fclose(pagemap);

   return page_frame_number;
}

int main(){

	/*
		Construindo o ponteiro para o espaço de memória compartilhada.
	*/
	

	int shm_fd;
	char *ptr;
	int teste_end_logico;
	const int SIZE = 4096;
	const char *name = "shared_memory";

	shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
	ftruncate(shm_fd,SIZE);

	ptr = mmap(0,SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (ptr == MAP_FAILED) {
		printf("Map failed\n");
		return -1;
	}


	/* 
	   Escrevendo uma mensagem qualquer no ponteiro para forçar o SO a mapear a página
	   com a variável do ponteiro em um quadro na memória. Inicialmente nem todas as páginas
	   de um processo estão mapeadas para a memória, o SO só as mapeia quando necessário. 
	*/
	sprintf(ptr,"Hello: ");

	unsigned int page_frame_number;
	uint64_t physical_addr;
	/*
		Como o offset para caminhar dentro de uma página é o mesmo para caminhar dentro de um
		quadro, o cálculo do offset é feito com base no resto da divisão do endereço pelo tama-
		nho da página, resultando no deslocamento a ser feito dentro do quadro.

		Como por exemplo, para uma varíavel com endereço 4098, o offset calculado resultaria
		em 4098%4096 = 2. Ou seja o deslocamento de 2 bytes quando utilizado no quadro resulta
		no acesso a variável de endereço 4098 da memória virtual.
	*/
	unsigned int frame_offset = (unsigned long)ptr % getpagesize();

	printf("\n\nHipóteses a serem testadas:\n");
	printf("1) Endereços lógicos iguais em pai e filho -> físicos diferentes\n");
	printf("2) Endereços lógicos iguais em pai e filho -> físicos iguais em um contexto de memória compartilhada\n");
	printf("-----------------------------------------\n\n");
	int res = fork () ;//Criando processo filho

	if(res>0){
		printf("1) Endereço lógico do pai: %p\n\n",&teste_end_logico);
		teste_end_logico = 10;
		page_frame_number = get_page_frame_number_of_address(&teste_end_logico);
		//printf("Endereço do quadro referenciado pelo endereço lógico do pai: %u\n\n", page_frame_number);
		physical_addr = (page_frame_number << PAGE_SHIFT) + frame_offset;	
		printf("1) Endereço físico referenciado pelo endereço lógico do pai: 0x%" PRIu64 "\n\n", physical_addr);

		printf("2) Endereço lógico de memória compartilhada do pai: %p\n\n",ptr);
		page_frame_number = get_page_frame_number_of_address(ptr);
		//printf("2) Endereço do quadro da memória compartilhada referenciado pelo endereço lógico do pai: %u\n\n", page_frame_number);
		physical_addr = (page_frame_number << PAGE_SHIFT) + frame_offset;	
		printf("2) Endereço físico referenciado da memória compartilhada pelo endereço lógico do pai: 0x%" PRIu64 "\n\n", physical_addr);
	}else{
		printf("1) Endereço lógico do filho: %p\n\n",&teste_end_logico);
		teste_end_logico = 11;
		page_frame_number = get_page_frame_number_of_address(&teste_end_logico);
		//printf("Endereço do quadro referenciado pelo endereço lógico do filho: %u\n\n", page_frame_number);
		physical_addr = (page_frame_number << PAGE_SHIFT) + frame_offset;	
		printf("1) Endereço físico referenciado pelo endereço lógico do filho: 0x%" PRIu64 "\n\n", physical_addr);


		/*
		   Mapeando o ponteiro novamente para provar a segunda hipótese proposta, de que para um contexto
		   de memória compartilhada, dois endereços lógico diferentes apontam para o mesmo físico. 
		*/
		ptr = mmap(0,SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		if (ptr == MAP_FAILED) {
			printf("Map failed\n");
			return -1;
		}

		sprintf(ptr,"Hello: ");

		/* Testando a segunda hipótese */
		printf("2) Endereço lógico de memória compartilhada do filho: %p\n\n",ptr);
		page_frame_number = get_page_frame_number_of_address(ptr);
		physical_addr = (page_frame_number << PAGE_SHIFT) + frame_offset;	
		printf("2) Endereço físico da memória compartilhada referenciado pelo endereço lógico do filho: 0x%" PRIu64 "\n\n", physical_addr);
	}


	return 0;
}
