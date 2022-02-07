
int delayTime = 200; 

void setup() {
pinMode(0, OUTPUT);
pinMode(1, OUTPUT);
pinMode(2, OUTPUT);
pinMode(3, OUTPUT);
pinMode(4, OUTPUT);

//Lamp test
digitalWrite(0, HIGH);
digitalWrite(1, HIGH);
digitalWrite(2, HIGH);
digitalWrite(3, HIGH);
digitalWrite(4, HIGH);
delay(2000);

//Lamp reset
digitalWrite(0, LOW);
digitalWrite(1, LOW);
digitalWrite(2, LOW);
digitalWrite(3, LOW);
digitalWrite(4, LOW);

}

//Chase sequence
//0 1 2...1 2 3...2 3 4...3 4 5

void loop() 
{

//s is start bulb, s is finish
int f=2

for (int s = 0; s <= 3; s++)
{
  for (int i = s; i <= f; i++) 
  {
    digitalWrite(i, HIGH);
    delay(delayTime);
  }

  //need to increment the "finish" too
  f++
}


}
