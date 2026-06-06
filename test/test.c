

int arr[10000];
int main()
{
    int k = 10;
    for (int i = 0; i < 200; i++)
    {
        int deadline = 0;
        k = k + 2;
        arr[k] = i;
    }
}