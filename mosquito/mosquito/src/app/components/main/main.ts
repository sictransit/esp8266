import { Component } from '@angular/core';
import { Sensitivity } from '../sensitivity/sensitivity';
import { Light } from '../light/light';

@Component({
  selector: 'app-main',
  imports: [ Sensitivity, Light ],
  templateUrl: './main.html',
  styleUrl: './main.scss'
})
export class Main {

}
